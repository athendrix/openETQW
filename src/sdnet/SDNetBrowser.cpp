// SPDX-License-Identifier: GPL-3.0-or-later
/*
The menu's server browser without a master server.

The SDK game's browser (openETQW-game src/game/sdnet/SDNetManager.cpp) asks the network
service's session manager for sessions: FindSessions for the LAN and Internet tabs,
RefreshSessions for the History and Favorites tabs (addresses kept in the player's
profile), RefreshSession for one server. Retail answered those through Demonware's master
server; the offline service answered nothing, so the browser stayed empty.

Here each tab is filled by asking the servers themselves (ServerQuery.h):
  LAN        a broadcast to the server port (27733), and the saved server list;
  Internet   the saved server list (net_serverList, edited with addServer, removeServer);
  History    the last servers joined (written here when a connect is accepted);
  Favorites  the servers starred in the browser.
The profile, which holds the history and the favorites, is saved to the save path
(SDNet.cpp), so both survive a restart.

Everything runs on the main thread: the game's UI calls in and the engine's frame calls
SDNet_BrowserFrame, so the tasks' locks have nothing to guard.
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "SDNet.h"
#include "SDNetUser.h"
#include "SDNetProfile.h"
#include "SDNetSession.h"
#include "SDNetSessionManager.h"
#include "SDNetTask.h"
#include "../framework/async/AsyncNetwork.h"
#include "../framework/async/ServerQuery.h"
#include "SDNetBrowser.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

idCVar net_browserMaxDifficulty( "net_browserMaxDifficulty", "24", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "the most proof of work (leading zero bits) the server browser does for one server; a server asking more is not listed", SQ_MIN_DIFFICULTY, SQ_MAX_DIFFICULTY );
idCVar net_serverList( "net_serverList", "", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "servers the menu's server browser lists (Internet tab), separated by spaces: host or host:port. Edit with addServer and removeServer" );

namespace {

const int BROWSER_RESEND_MSEC		= 700;
const int BROWSER_TRIES				= 3;
const int BROWSER_FIND_MSEC			= 4000;		// a find ends this long after it starts
const int BROWSER_REFRESH_MSEC		= 3000;
const int BROWSER_NO_PING			= 999;		// what the game shows as no answer

class sdNetSessionBrowser : public sdNetSession {
public:
	sdNetSessionBrowser( const netadr_t& address ) : address( address ), ping( BROWSER_NO_PING ), numBots( 0 ), gameState( 0 ), timeLeft( 0 ) {
		addressString = Sys_NetAdrToString( address );
	}

	void Update( const sqInfo_t& info, int newPing ) {
		serverInfo.Clear();
		for ( int i = 0; i < info.numKeys; i++ ) {
			serverInfo.Set( info.keys[ i ], info.values[ i ] );
		}
		clients.SetNum( 0, false );
		numBots = 0;
		for ( int i = 0; i < info.numPlayers; i++ ) {
			sessionClientInfo_t& client = clients.Alloc();
			idStr::Copynz( client.nickname, info.players[ i ].name, sizeof( client.nickname ) );
			client.ping = static_cast< short >( info.players[ i ].ping );
			client.rate = 0;
			client.connected = true;
			client.isBot = info.players[ i ].bot;
			if ( client.isBot ) {
				numBots++;
			}
		}
		ping = idMath::ClampInt( 0, BROWSER_NO_PING - 1, newPing );
		gameState = static_cast< byte >( info.gameState );
		timeLeft = info.timeLeft;
	}

	virtual sessionState_e		GetState() const { return SS_IDLE; }
	virtual const char*			GetHostName() const { return addressString.c_str(); }
	virtual const char*			GetHostAddressString() const { return addressString.c_str(); }
	virtual const netadr_t&		GetAddress() const { return address; }
	virtual const idDict&		GetServerInfo() const { return serverInfo; }
	virtual idDict&				GetServerInfo() { return serverInfo; }
	virtual const int			GetNumClients() const { return clients.Num(); }
	virtual const int			GetNumBotClients() const { return numBots; }
	virtual const sessionClientInfo_t& GetClientInfo( int clientNum ) const {
		static sessionClientInfo_t none;
		return clientNum >= 0 && clientNum < clients.Num() ? clients[ clientNum ] : none;
	}
	virtual const int			GetPing() const { return ping; }
	virtual bool				IsRanked() const { return false; }
	virtual byte				GetGameState() const { return gameState; }
	virtual int					GetSessionTime() const { return timeLeft; }
	virtual bool				Join() {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "connect %s\n", addressString.c_str() ) );
		return true;
	}
	virtual void				GetId( sessionId_t& sessionId ) const {
		sessionId.Clear();
		memcpy( sessionId.id, address.ip, 4 );
		sessionId.id[ 4 ] = static_cast< byte >( address.port & 0xff );
		sessionId.id[ 5 ] = static_cast< byte >( address.port >> 8 );
	}
	virtual void				ServerClientConnect( const int ) {}
	virtual void				ServerClientDisconnect( const int ) {}
	virtual int					GetNumInterestedClients( void ) const { return 0; }
	virtual bool				IsRepeater() const { return false; }
	virtual int					GetNumRepeaterClients( void ) const { return 0; }
	virtual int					GetMaxRepeaterClients( void ) const { return 0; }

private:
	netadr_t						address;
	idStr							addressString;
	idDict							serverInfo;
	idList< sessionClientInfo_t >	clients;
	int								ping;
	int								numBots;
	byte							gameState;
	int								timeLeft;
};

class sdNetTaskBrowser : public sdNetTask {
public:
	sdNetTaskBrowser() : state( TS_PENDING ), findList( NULL ), deadline( 0 ) {}

	virtual void				Cancel( bool ) { state = TS_DONE; }
	virtual taskStatus_e		GetState() const { return state; }
	virtual sdNetErrorCode_e	GetErrorCode() const { return SDNET_NO_ERROR; }
	virtual void				AcquireLock() {}
	virtual void				ReleaseLock() {}

	taskStatus_e				state;
	idList< sdNetSession* >*	findList;	// a find appends the servers that answer; NULL for a refresh
	int							deadline;
};

/*
The proof of work runs on one worker thread, so the menu never waits on it: a 16-bit
puzzle is about 65,000 hashes, some tens of milliseconds.
*/
class sdPowSolver {
public:
	sdPowSolver() : stop( false ), started( false ) {}
	~sdPowSolver() { Stop(); }

	void Submit( int id, const uint8_t cookie[ SQ_COOKIE_SIZE ], int difficulty ) {
		std::lock_guard< std::mutex > lock( mutex );
		if ( !started ) {
			stop = false;
			worker = std::thread( &sdPowSolver::Run, this );
			started = true;
		}
		job_t job;
		job.id = id;
		memcpy( job.cookie, cookie, SQ_COOKIE_SIZE );
		job.difficulty = difficulty;
		jobs.push_back( job );
		wake.notify_one();
	}

	// drops a job not started yet; one already running finishes and its result is ignored
	void Cancel( int id ) {
		std::lock_guard< std::mutex > lock( mutex );
		for ( size_t i = 0; i < jobs.size(); i++ ) {
			if ( jobs[ i ].id == id ) {
				jobs.erase( jobs.begin() + i );
				return;
			}
		}
	}

	bool TakeResult( int& id, uint64_t& work ) {
		std::lock_guard< std::mutex > lock( mutex );
		if ( results.empty() ) {
			return false;
		}
		id = results.front().first;
		work = results.front().second;
		results.pop_front();
		return true;
	}

	void Stop() {
		{
			std::lock_guard< std::mutex > lock( mutex );
			if ( !started ) {
				return;
			}
			stop = true;
			jobs.clear();
			wake.notify_one();
		}
		worker.join();
		std::lock_guard< std::mutex > lock( mutex );
		results.clear();
		started = false;
	}

private:
	struct job_t {
		int		id;
		uint8_t	cookie[ SQ_COOKIE_SIZE ];
		int		difficulty;
	};

	void Run() {
		while ( true ) {
			job_t job;
			{
				std::unique_lock< std::mutex > lock( mutex );
				wake.wait( lock, [this] { return stop || !jobs.empty(); } );
				if ( stop ) {
					return;
				}
				job = jobs.front();
				jobs.pop_front();
			}
			uint64_t work = 0;
			bool found = false;
			while ( !found ) {
				found = SQ_SolveWork( job.cookie, job.difficulty, work, 4096 );
				std::lock_guard< std::mutex > lock( mutex );
				if ( stop ) {
					return;
				}
			}
			std::lock_guard< std::mutex > lock( mutex );
			results.push_back( std::make_pair( job.id, work ) );
		}
	}

	std::mutex								mutex;
	std::condition_variable					wake;
	std::deque< job_t >						jobs;
	std::deque< std::pair< int, uint64_t > >	results;
	std::thread								worker;
	bool									stop;
	bool									started;
};

enum queryStage_t {
	QS_CHALLENGE,	// asked for a cookie (step 1)
	QS_SOLVING,		// the worker is doing the proof of work
	QS_INFO,		// asked for the info (step 3)
	QS_DONE			// answered, or gave up
};

struct query_t {
	int						id;
	sdNetTaskBrowser*		task;
	sdNetSessionBrowser*	session;	// a refresh's session; NULL for a find
	netadr_t				address;
	bool					broadcast;	// a LAN broadcast: each server answering gets a query of its own
	int						nonce;
	int						lastSent;
	int						tries;
	queryStage_t			stage;
	sqChallenge_t			challenge;
	uint64_t				work;
};

class sdServerBrowser {
public:
	sdServerBrowser() : nextNonce( 1 ), nextId( 1 ) {}

	sdNetSession* AllocSession( const netadr_t* address ) {
		netadr_t adr;
		memset( &adr, 0, sizeof( adr ) );
		adr.type = NA_BAD;
		if ( address != NULL ) {
			adr = *address;
		}
		return new sdNetSessionBrowser( adr );
	}

	void FreeSession( sdNetSession* session ) {
		for ( int i = queries.Num() - 1; i >= 0; i-- ) {
			if ( queries[ i ].session == session ) {
				RemoveQuery( i );
			}
		}
		delete static_cast< sdNetSessionBrowser* >( session );
	}

	sdNetTask* Find( idList< sdNetSession* >& sessions, sdNetSessionManager::sessionSource_e source ) {
		if ( !InitPort() ) {
			return NULL;
		}
		sdNetTaskBrowser* task = NewTask();
		task->findList = &sessions;
		task->deadline = Sys_Milliseconds() + BROWSER_FIND_MSEC;

		const bool lan = source == sdNetSessionManager::SS_LAN;
		const bool internet = source == sdNetSessionManager::SS_INTERNET_ALL || source == sdNetSessionManager::SS_INTERNET_RANKED;
		if ( lan ) {
			netadr_t adr;
			memset( &adr, 0, sizeof( adr ) );
			adr.type = NA_BROADCAST;
			adr.port = PORT_SERVER;
			AddQuery( task, NULL, adr, true );
		}
		if ( lan || internet ) {
			idList< netadr_t > listed;
			ListedServers( listed );
			for ( int i = 0; i < listed.Num(); i++ ) {
				AddQuery( task, NULL, listed[ i ], false );
			}
		}
		// repeater sources (ETQW TV) have nothing to find
		if ( !lan && !internet ) {
			task->state = sdNetTask::TS_DONE;
		}
		return task;
	}

	sdNetTask* Refresh( idList< sdNetSession* >& sessions ) {
		if ( !InitPort() ) {
			return NULL;
		}
		sdNetTaskBrowser* task = NewTask();
		task->deadline = Sys_Milliseconds() + BROWSER_REFRESH_MSEC;
		int asked = 0;
		for ( int i = 0; i < sessions.Num(); i++ ) {
			sdNetSessionBrowser* session = static_cast< sdNetSessionBrowser* >( sessions[ i ] );
			if ( session != NULL && session->GetAddress().type == NA_IP ) {
				AddQuery( task, session, session->GetAddress(), false );
				asked++;
			}
		}
		if ( asked == 0 ) {
			task->state = sdNetTask::TS_DONE;
		}
		return task;
	}

	void FreeTask( sdNetTask* task ) {
		for ( int i = queries.Num() - 1; i >= 0; i-- ) {
			if ( queries[ i ].task == task ) {
				RemoveQuery( i );
			}
		}
		tasks.Remove( static_cast< sdNetTaskBrowser* >( task ) );
		delete static_cast< sdNetTaskBrowser* >( task );
	}

	void Frame() {
		if ( port.GetPort() == 0 ) {
			return;
		}
		const int now = Sys_Milliseconds();
		ReadAnswers( now );
		TakeSolutions( now );

		// resend what has not been answered: a lost cookie or a lost info answer starts
		// over with a new cookie (the server answers a cookie once)
		for ( int i = 0; i < queries.Num(); i++ ) {
			query_t& query = queries[ i ];
			if ( query.task->state == sdNetTask::TS_DONE || query.stage == QS_DONE || query.stage == QS_SOLVING ) {
				continue;
			}
			if ( now - query.lastSent < BROWSER_RESEND_MSEC ) {
				continue;
			}
			if ( query.tries >= BROWSER_TRIES ) {
				if ( !query.broadcast ) {
					query.stage = QS_DONE;
				}
				continue;
			}
			query.stage = QS_CHALLENGE;
			SendChallengeRequest( query, now );
		}

		// a task is done at its deadline, or once every server it asked has answered or
		// used up its tries; a LAN broadcast can be answered by anyone, so it waits for
		// the deadline
		for ( int i = 0; i < tasks.Num(); i++ ) {
			sdNetTaskBrowser* task = tasks[ i ];
			if ( task->state == sdNetTask::TS_DONE ) {
				continue;
			}
			bool outstanding = false;
			for ( int j = 0; j < queries.Num(); j++ ) {
				const query_t& query = queries[ j ];
				if ( query.task == task && ( query.broadcast || query.stage != QS_DONE ) ) {
					outstanding = true;
				}
			}
			if ( now >= task->deadline || !outstanding ) {
				task->state = sdNetTask::TS_DONE;
				for ( int j = queries.Num() - 1; j >= 0; j-- ) {
					if ( queries[ j ].task == task ) {
						RemoveQuery( j );
					}
				}
			}
		}
	}

	void Shutdown() {
		solver.Stop();
		queries.Clear();
		for ( int i = 0; i < tasks.Num(); i++ ) {
			tasks[ i ]->state = sdNetTask::TS_DONE;
		}
		port.Close();
	}

	// the servers in net_serverList, resolved
	static void ListedServers( idList< netadr_t >& out ) {
		idStrList entries;
		SplitList( net_serverList.GetString(), entries );
		for ( int i = 0; i < entries.Num(); i++ ) {
			netadr_t adr;
			if ( !Sys_StringToNetAdr( entries[ i ].c_str(), &adr, true ) || adr.type != NA_IP ) {
				common->Printf( "server list: can't resolve '%s'\n", entries[ i ].c_str() );
				continue;
			}
			if ( adr.port == 0 ) {
				adr.port = PORT_SERVER;
			}
			out.Append( adr );
		}
	}

	static void SplitList( const char* text, idStrList& out ) {
		idStr current;
		for ( const char* c = text; ; c++ ) {
			if ( *c == '\0' || *c == ' ' || *c == ',' || *c == ';' || *c == '\t' ) {
				if ( current.Length() > 0 ) {
					out.Append( current );
					current.Clear();
				}
				if ( *c == '\0' ) {
					break;
				}
			} else {
				current += *c;
			}
		}
	}

private:
	sdNetTaskBrowser* NewTask() {
		sdNetTaskBrowser* task = new sdNetTaskBrowser;
		tasks.Append( task );
		return task;
	}

	bool InitPort() {
		if ( port.GetPort() != 0 ) {
			return true;
		}
		if ( !port.InitForPort( PORT_ANY ) ) {
			common->Warning( "server browser: couldn't open a network port" );
			return false;
		}
		return true;
	}

	query_t& NewQuery( sdNetTaskBrowser* task, sdNetSessionBrowser* session, const netadr_t& address, bool broadcast, int nonce ) {
		query_t& query = queries.Alloc();
		memset( &query, 0, sizeof( query ) );
		query.id = nextId++;
		query.task = task;
		query.session = session;
		query.address = address;
		query.broadcast = broadcast;
		query.nonce = nonce;
		query.stage = QS_CHALLENGE;
		return query;
	}

	void AddQuery( sdNetTaskBrowser* task, sdNetSessionBrowser* session, const netadr_t& address, bool broadcast ) {
		query_t& query = NewQuery( task, session, address, broadcast, nextNonce++ & 0x7fffffff );
		SendChallengeRequest( query, Sys_Milliseconds() );
	}

	void RemoveQuery( int index ) {
		if ( queries[ index ].stage == QS_SOLVING ) {
			solver.Cancel( queries[ index ].id );
		}
		queries.RemoveIndex( index );
	}

	void SendChallengeRequest( query_t& query, int now ) {
		byte buffer[ 128 ];
		idBitMsg msg;
		msg.InitWrite( buffer, sizeof( buffer ) );
		msg.BeginWriting();
		SQ_WriteChallengeRequest( msg, ASYNC_PROTOCOL_VERSION, query.nonce );
		port.SendPacket( query.address, msg.GetData(), msg.GetSize() );
		query.lastSent = now;
		query.tries++;
	}

	void SendInfoRequest( query_t& query, int now ) {
		byte buffer[ 128 ];
		idBitMsg msg;
		msg.InitWrite( buffer, sizeof( buffer ) );
		msg.BeginWriting();
		SQ_WriteInfoRequest( msg, query.challenge, query.work );
		port.SendPacket( query.address, msg.GetData(), msg.GetSize() );
		query.lastSent = now;
		query.stage = QS_INFO;
	}

	void StartSolving( query_t& query, const sqChallenge_t& challenge ) {
		query.challenge = challenge;
		query.stage = QS_SOLVING;
		solver.Submit( query.id, challenge.cookie, challenge.difficulty );
	}

	void TakeSolutions( int now ) {
		int id = 0;
		uint64_t work = 0;
		while ( solver.TakeResult( id, work ) ) {
			for ( int i = 0; i < queries.Num(); i++ ) {
				if ( queries[ i ].id == id && queries[ i ].stage == QS_SOLVING ) {
					queries[ i ].work = work;
					SendInfoRequest( queries[ i ], now );
					break;
				}
			}
		}
	}

	void ReadAnswers( int now ) {
		byte buffer[ MAX_MESSAGE_SIZE ];
		netadr_t from;
		int size = 0;
		sqInfo_t* info = NULL;
		while ( port.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
			idBitMsg msg;
			msg.InitWrite( buffer, sizeof( buffer ) );
			msg.SetSize( size );
			msg.BeginReading();
			if ( msg.ReadShort() != CONNECTIONLESS_MESSAGE_ID ) {
				continue;
			}
			char command[ 32 ];
			msg.ReadString( command, sizeof( command ) );
			if ( idStr::Icmp( command, SQ_CHALLENGE ) == 0 ) {
				sqChallenge_t challenge;
				if ( SQ_ReadChallenge( msg, challenge ) && challenge.protocol == ASYNC_PROTOCOL_VERSION ) {
					Challenged( from, challenge );
				}
			} else if ( idStr::Icmp( command, SQ_RESPONSE ) == 0 ) {
				if ( info == NULL ) {
					info = new sqInfo_t;
				}
				if ( SQ_ReadResponse( msg, *info ) && info->protocol == ASYNC_PROTOCOL_VERSION ) {
					Answered( from, *info, now );
				}
			}
		}
		delete info;
	}

	static bool SameAddress( const netadr_t& a, const netadr_t& b ) {
		return Sys_CompareNetAdrBase( a, b ) && a.port == b.port;
	}

	void Challenged( const netadr_t& from, const sqChallenge_t& challenge ) {
		if ( challenge.difficulty > net_browserMaxDifficulty.GetInteger() ) {
			return;	// more work than we are willing to do for one server
		}
		for ( int i = 0; i < queries.Num(); i++ ) {
			query_t& query = queries[ i ];
			if ( query.nonce != challenge.nonce || query.task->state == sdNetTask::TS_DONE ) {
				continue;
			}
			if ( !query.broadcast ) {
				if ( query.stage == QS_CHALLENGE && SameAddress( from, query.address ) ) {
					StartSolving( query, challenge );
				}
				continue;
			}
			// a server answering a broadcast gets a query of its own, once
			sdNetTaskBrowser* task = query.task;
			for ( int j = 0; j < queries.Num(); j++ ) {
				if ( queries[ j ].task == task && !queries[ j ].broadcast && SameAddress( queries[ j ].address, from ) ) {
					return;
				}
			}
			query_t& found = NewQuery( task, NULL, from, false, challenge.nonce );
			found.tries = 1;
			StartSolving( found, challenge );
			return;
		}
	}

	void Answered( const netadr_t& from, const sqInfo_t& info, int now ) {
		for ( int i = 0; i < queries.Num(); i++ ) {
			query_t& query = queries[ i ];
			if ( query.nonce != info.nonce || query.stage != QS_INFO || query.broadcast || !SameAddress( from, query.address ) ) {
				continue;
			}
			query.stage = QS_DONE;
			const int ping = now - query.lastSent;
			if ( query.session != NULL ) {
				query.session->Update( info, ping );
				return;
			}
			// a find: one session per server
			idList< sdNetSession* >& list = *query.task->findList;
			sdNetSessionBrowser* session = NULL;
			for ( int j = 0; j < list.Num(); j++ ) {
				if ( SameAddress( list[ j ]->GetAddress(), from ) ) {
					session = static_cast< sdNetSessionBrowser* >( list[ j ] );
					break;
				}
			}
			if ( session == NULL ) {
				session = new sdNetSessionBrowser( from );
				list.Append( session );
			}
			session->Update( info, ping );
			return;
		}
	}

	idPort						port;
	idList< query_t >			queries;
	idList< sdNetTaskBrowser* >	tasks;
	sdPowSolver					solver;
	int							nextNonce;
	int							nextId;
};

sdServerBrowser* browser = NULL;

sdServerBrowser& Browser() {
	if ( browser == NULL ) {
		browser = new sdServerBrowser;
	}
	return *browser;
}

bool ListHas( const idStrList& entries, const char* address ) {
	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[ i ].Icmp( address ) == 0 ) {
			return true;
		}
	}
	return false;
}

void SetList( const idStrList& entries ) {
	idStr text;
	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( i > 0 ) {
			text += " ";
		}
		text += entries[ i ];
	}
	net_serverList.SetString( text.c_str() );
}

// addServer <host[:port]>: adds a server to the browser's Internet tab
void AddServer_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: addServer <host or host:port>\n" );
		return;
	}
	const char* address = args.Argv( 1 );
	netadr_t adr;
	if ( !Sys_StringToNetAdr( address, &adr, true ) || adr.type != NA_IP ) {
		common->Printf( "addServer: can't resolve '%s'\n", address );
		return;
	}
	idStrList entries;
	sdServerBrowser::SplitList( net_serverList.GetString(), entries );
	if ( ListHas( entries, address ) ) {
		common->Printf( "addServer: '%s' is already in the list\n", address );
		return;
	}
	entries.Append( address );
	SetList( entries );
	common->Printf( "addServer: added '%s' (%s)\n", address, Sys_NetAdrToString( adr ) );
}

void RemoveServer_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: removeServer <host or host:port, as in serverList>\n" );
		return;
	}
	idStrList entries;
	sdServerBrowser::SplitList( net_serverList.GetString(), entries );
	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[ i ].Icmp( args.Argv( 1 ) ) == 0 ) {
			entries.RemoveIndex( i );
			SetList( entries );
			common->Printf( "removeServer: removed '%s'\n", args.Argv( 1 ) );
			return;
		}
	}
	common->Printf( "removeServer: '%s' is not in the list\n", args.Argv( 1 ) );
}

void ServerList_f( const idCmdArgs& ) {
	idStrList entries;
	sdServerBrowser::SplitList( net_serverList.GetString(), entries );
	if ( entries.Num() == 0 ) {
		common->Printf( "the server list is empty; add a server with addServer <host or host:port>\n" );
	}
	for ( int i = 0; i < entries.Num(); i++ ) {
		common->Printf( "  %s\n", entries[ i ].c_str() );
	}
}

/*
queryServers [lan]: the browser's Internet tab (or LAN tab) in the console, for a
server operator checking that the server answers, and for the tests.
*/
struct consoleQuery_t {
	sdNetTask*				task;
	idList< sdNetSession* >	sessions;
	bool					detail;		// serverInfo: every key and player
	consoleQuery_t() : task( NULL ), detail( false ) {}
};
consoleQuery_t* consoleQuery = NULL;

void QueryServers_f( const idCmdArgs& args ) {
	if ( consoleQuery != NULL ) {
		common->Printf( "queryServers: still waiting for answers\n" );
		return;
	}
	const bool lan = idStr::Icmp( args.Argv( 1 ), "lan" ) == 0;
	consoleQuery = new consoleQuery_t;
	consoleQuery->task = Browser().Find( consoleQuery->sessions, lan ? sdNetSessionManager::SS_LAN : sdNetSessionManager::SS_INTERNET_ALL );
	if ( consoleQuery->task == NULL ) {
		delete consoleQuery;
		consoleQuery = NULL;
		return;
	}
	common->Printf( "queryServers: asking %s...\n", lan ? "the LAN and the server list" : "the server list" );
}

void ConsoleQueryFrame() {
	if ( consoleQuery == NULL || consoleQuery->task->GetState() != sdNetTask::TS_DONE ) {
		return;
	}
	if ( consoleQuery->detail ) {
		sdNetSession* session = consoleQuery->sessions[ 0 ];
		if ( session->GetPing() >= BROWSER_NO_PING ) {
			common->Printf( "serverInfo: %s did not answer\n", session->GetHostAddressString() );
		} else {
			const idDict& info = session->GetServerInfo();
			common->Printf( "serverInfo: %s, %d ms\n", session->GetHostAddressString(), session->GetPing() );
			for ( int i = 0; i < info.GetNumKeyVals(); i++ ) {
				common->Printf( "  %-24s %s\n", info.GetKeyVal( i )->GetKey().c_str(), info.GetKeyVal( i )->GetValue().c_str() );
			}
			common->Printf( "  %d player%s:\n", session->GetNumClients(), session->GetNumClients() == 1 ? "" : "s" );
			for ( int i = 0; i < session->GetNumClients(); i++ ) {
				const sdNetSession::sessionClientInfo_t& client = session->GetClientInfo( i );
				common->Printf( "    %-32s %s\n", client.nickname, client.isBot ? "bot" : va( "%d ms", client.ping ) );
			}
		}
	}
	if ( !consoleQuery->detail ) {
		common->Printf( "queryServers: %d server%s answered\n", consoleQuery->sessions.Num(), consoleQuery->sessions.Num() == 1 ? "" : "s" );
	}
	for ( int i = 0; i < consoleQuery->sessions.Num() && !consoleQuery->detail; i++ ) {
		sdNetSession* session = consoleQuery->sessions[ i ];
		const idDict& info = session->GetServerInfo();
		const int seconds = session->GetSessionTime() / 1000;
		common->Printf( "  %-21s %4dms  %d/%d  %s  %s  state %d  %d:%02d left  %s\n", session->GetHostAddressString(), session->GetPing(),
			session->GetNumClients(), info.GetInt( "si_maxPlayers" ), info.GetString( "si_map" ),
			info.GetString( "si_rules" ), session->GetGameState(), seconds / 60, seconds % 60, info.GetString( "si_name" ) );
	}
	Browser().FreeTask( consoleQuery->task );
	for ( int i = 0; i < consoleQuery->sessions.Num(); i++ ) {
		Browser().FreeSession( consoleQuery->sessions[ i ] );
	}
	delete consoleQuery;
	consoleQuery = NULL;
}

} // namespace

/*
================
sdNetSessionManagerBrowser
================
*/
sdNetSessionManagerBrowser::sdNetSessionManagerBrowser() {
	memset( &currentAddress, 0, sizeof( currentAddress ) );
	currentAddress.type = NA_BAD;
}

sdNetSession* sdNetSessionManagerBrowser::AllocSession( const netadr_t* sessionAddress ) {
	return Browser().AllocSession( sessionAddress );
}

void sdNetSessionManagerBrowser::FreeSession( sdNetSession* session ) {
	if ( session != NULL ) {
		Browser().FreeSession( session );
	}
}

sdNetTask* sdNetSessionManagerBrowser::FindSessions( idList< sdNetSession* >& sessions, sessionSource_e source ) {
	return Browser().Find( sessions, source );
}

sdNetTask* sdNetSessionManagerBrowser::RefreshSessions( idList< sdNetSession* >& sessions ) {
	return Browser().Refresh( sessions );
}

sdNetTask* sdNetSessionManagerBrowser::RefreshSession( sdNetSession& session ) {
	idList< sdNetSession* > one;
	one.Append( &session );
	return Browser().Refresh( one );
}

// serverInfo <host[:port]>: one server's settings and players (AsyncNetwork.cpp)
void SDNet_BrowserServerInfo( const char* address ) {
	if ( consoleQuery != NULL ) {
		common->Printf( "serverInfo: still waiting for answers\n" );
		return;
	}
	netadr_t adr;
	if ( address == NULL || !Sys_StringToNetAdr( address, &adr, true ) || adr.type != NA_IP ) {
		common->Printf( "serverInfo: can't resolve '%s'\n", address != NULL ? address : "" );
		return;
	}
	if ( adr.port == 0 ) {
		adr.port = PORT_SERVER;
	}
	consoleQuery = new consoleQuery_t;
	consoleQuery->detail = true;
	consoleQuery->sessions.Append( Browser().AllocSession( &adr ) );
	consoleQuery->task = Browser().Refresh( consoleQuery->sessions );
	if ( consoleQuery->task == NULL ) {
		Browser().FreeSession( consoleQuery->sessions[ 0 ] );
		delete consoleQuery;
		consoleQuery = NULL;
		return;
	}
	common->Printf( "serverInfo: asking %s...\n", Sys_NetAdrToString( adr ) );
}

void SDNet_BrowserFrame( void ) {
	if ( browser != NULL ) {
		browser->Frame();
		ConsoleQueryFrame();
	}
}

void SDNet_BrowserFreeTask( sdNetTask* task ) {
	if ( task != NULL ) {
		Browser().FreeTask( task );
	}
}

void SDNet_BrowserShutdown( void ) {
	if ( browser != NULL ) {
		browser->Shutdown();
	}
}

void SDNet_BrowserRegisterCommands( void ) {
	cmdSystem->AddCommand( "addServer", AddServer_f, CMD_FL_SYSTEM, "adds a server (host or host:port) to the menu's server list" );
	cmdSystem->AddCommand( "removeServer", RemoveServer_f, CMD_FL_SYSTEM, "removes a server from the menu's server list" );
	cmdSystem->AddCommand( "serverList", ServerList_f, CMD_FL_SYSTEM, "prints the menu's server list" );
	cmdSystem->AddCommand( "queryServers", QueryServers_f, CMD_FL_SYSTEM, "asks the servers in the server list (and with 'lan', the LAN) and prints their answers" );
}

/*
================
SDNet_BrowserServerJoined

A connect was accepted (idAsyncClient::ProcessConnectResponseMessage): the server goes to
the top of the History tab (serverHistory0..9 in the profile, as the SDK game reads it).
================
*/
void SDNet_BrowserServerJoined( const netadr_t& address ) {
	if ( networkService == NULL || address.type != NA_IP ) {
		return;
	}
	sdNetUser* user = networkService->GetActiveUser();
	if ( user == NULL ) {
		return;
	}
	idDict& properties = user->GetProfile().GetProperties();
	const idStr joined = Sys_NetAdrToString( address );

	idStrList history;
	history.Append( joined );
	for ( int i = 0; i < sdNetUser::MAX_SERVER_HISTORY; i++ ) {
		const char* entry = properties.GetString( va( "serverHistory%i", i ), "" );
		if ( entry[ 0 ] != '\0' && joined.Icmp( entry ) != 0 && history.Num() < sdNetUser::MAX_SERVER_HISTORY ) {
			history.Append( entry );
		}
	}
	for ( int i = 0; i < sdNetUser::MAX_SERVER_HISTORY; i++ ) {
		properties.Set( va( "serverHistory%i", i ), i < history.Num() ? history[ i ].c_str() : "" );
	}
	user->Save( sdNetUser::SI_PROFILE );
}
