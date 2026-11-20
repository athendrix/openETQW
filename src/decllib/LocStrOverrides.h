// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __DECLLIB_LOCSTROVERRIDES_H__
#define __DECLLIB_LOCSTROVERRIDES_H__

/*
Menu text openETQW changes (decllocstr.cpp applies it after parsing, in every language):
without online accounts or ETQW TV, the main menu's LAN and computer tabs are the
multiplayer and singleplayer games, and the join dialog's LAN TV entry is the Internet
list (the game's sdNetManager::GuiServerSource, ApplyMenuOverrides).
*/

struct locStrOverride_t {
	const char*		name;
	const wchar_t*	text;
};

static const locStrOverride_t LOCSTR_OVERRIDES[] = {
	{ "guis/mainmenu/playlan",	L"Play Multiplayer" },
	{ "guis/mainmenu/playcomp",	L"Play Singleplayer" },
	{ "guis/mainmenu/lan_tv",	L"Internet" },
};

#endif /* !__DECLLIB_LOCSTROVERRIDES_H__ */
