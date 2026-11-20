// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SDNET_SDNETBROWSER_H__
#define __SDNET_SDNETBROWSER_H__

/*
The offline service's session manager: the menu's server browser, filled by asking the
servers themselves (SDNetBrowser.cpp).
*/
class sdNetSessionManagerBrowser : public sdNetSessionManager {
public:
							sdNetSessionManagerBrowser();

	virtual const netadr_t&	GetCurrentSessionAddress() const { return currentAddress; }
	virtual sdNetSession*	AllocSession( const netadr_t* sessionAddress = NULL );
	virtual void			FreeSession( sdNetSession* session );
	// advertising a session is the master server's job; there is none
	virtual sdNetTask*		CreateSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask*		UpdateSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask*		DeleteSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask*		FindSessions( idList< sdNetSession* >& sessions, sessionSource_e source = SS_INTERNET_ALL );
	virtual sdNetTask*		RefreshSessions( idList< sdNetSession* >& sessions );
	virtual sdNetTask*		RefreshSession( sdNetSession& session );

private:
	netadr_t				currentAddress;
};

void	SDNet_BrowserFrame( void );
void	SDNet_BrowserFreeTask( sdNetTask* task );
void	SDNet_BrowserShutdown( void );
void	SDNet_BrowserRegisterCommands( void );
void	SDNet_BrowserServerJoined( const netadr_t& address );	// a connect was accepted
bool	SDNet_BrowserLanServerSeen( void );	// a server on the LAN has answered since the game started
void	SDNet_BrowserAnnounce( int gamePort );	// a listen server's DHT announce through the browser's node (0: stop)

#endif /* !__SDNET_SDNETBROWSER_H__ */
