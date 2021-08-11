/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#include <stdbool.h>

#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <acfutils/airportdb.h>
#include <acfutils/crc64.h>
#include <acfutils/helpers.h>
#include <acfutils/osrand.h>
#include <acfutils/perf.h>

#include <opengpws/xplane_api.h>

#include "../src/libradio/navaiddb.h"
#include "../src/libradio/navrad.h"

#define	PLUGIN_NAME		"libradio by Saso Kiselkov"
#define	PLUGIN_SIG		"skiselkov.libradio"
#define	PLUGIN_DESCRIPTION	"libradio high fidelity nav radio simulation"

static bool		inited = false;
static char		xpdir[512] = { 0 };
static airportdb_t	adb = {};
static navaiddb_t	*ndb = NULL;
const egpws_intf_t	*egpws_intf = NULL;

static void
log_dbg_string(const char *str)
{
	XPLMDebugString(str);
}

PLUGIN_API int
XPluginStart(char *name, char *sig, char *desc)
{
	uint64_t seed;

	/* Always use Unix-native paths on the Mac! */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
	XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);

	XPLMGetSystemPath(xpdir);
	fix_pathsep(xpdir);
	/* Strip a trailing '/' from xpdir */
	if (xpdir[strlen(xpdir) - 1] == '/' ||
	    xpdir[strlen(xpdir) - 1] == '\\') {
		xpdir[strlen(xpdir) - 1] = '\0';
	}
	log_init(log_dbg_string, "libradio.plugin");
	logMsg("This is libradio.plugin");

	crc64_init();
	if (!osrand(&seed, sizeof (seed)))
		seed = microclock() + clock();
	crc64_srand(seed);

	strcpy(name, PLUGIN_NAME);
	strcpy(sig, PLUGIN_SIG);
	strcpy(desc, PLUGIN_DESCRIPTION);

	logMsg("libradio.plugin load complete");

	return (1);
}

PLUGIN_API void
XPluginStop(void)
{
	logMsg("libradio.plugin is unloading");

	navrad_fini();

	logMsg("libradio.plugin unload complete");
	log_fini();
}

PLUGIN_API void
XPluginDisable(void)
{
	if (!inited)
		return;
	inited = false;

	logMsg("navrad_fini");
	navrad_fini();
	if (ndb != NULL) {
		logMsg("navaiddb_destroy");
		navaiddb_destroy(ndb);
		ndb = NULL;
	}
	logMsg("airportdb_destroy");
	airportdb_destroy(&adb);
	logMsg("libradio.plugin disable complete");
}

static bool
opengpws_intf_init(void)
{
#if	LIBRADIO_OPENGPWS_CTL
	static const egpws_conf_t egpws_conf = { .type = EGPWS_DB_ONLY };
	static const egpws_range_t ranges[] = {
	    { NM2MET(10), 100 },
	    { NM2MET(25), 175 },
	    { NM2MET(50), 250 },
	    { NM2MET(350), 500 },
	    { NAN, NAN }
	};
#endif	/* LIBRADIO_OPENGPWS_CTL */
	XPLMPluginID plugin = XPLMFindPluginBySignature(OPENGPWS_PLUGIN_SIG);

	if (plugin == XPLM_NO_PLUGIN_ID) {
		logMsg("FATAL ERROR: OpenGPWS plugin not found. "
		    "Your installation appears broken. "
		    "Please reinstall the aircraft.");
		return (false);
	}
	XPLMSendMessageToPlugin(plugin, EGPWS_GET_INTF, &egpws_intf);
	VERIFY(egpws_intf != NULL);

#if	LIBRADIO_OPENGPWS_CTL
	egpws_intf->set_state(&egpws_conf);
	egpws_intf->set_pos_ok(true);
	egpws_intf->set_ranges(ranges);
#endif	/* LIBRADIO_OPENGPWS_CTL */

	return (true);
}

PLUGIN_API int
XPluginEnable(void)
{
	char *cachedir;

	ASSERT(!inited);
	inited = true;

	logMsg("airportdb_create");
	cachedir = mkpathname(xpdir, "Output", "caches",
	    "libradio.plugin", "airport.db", NULL);
	airportdb_create(&adb, xpdir, cachedir);
	lacf_free(cachedir);
	if (!recreate_cache(&adb)) {
		logMsg("recreate_cache failed, bailing");
		goto errout;
	}
	logMsg("navaiddb_init");
	ndb = navaiddb_create(xpdir, &adb);
	if (ndb == NULL) {
		logMsg("navaiddb_create failed, bailing");
		goto errout;
	}
	logMsg("navrad_init");
	if (!navrad_init(ndb)) {
		logMsg("navrad_init failed, bailing");
		goto errout;
	}
	logMsg("opengpws_intf_init");
	if (!opengpws_intf_init()) {
		logMsg("opengpws_intf_init failed, bailing");
		goto errout;
	}
	logMsg("libradio.plugin enable complete");

	return (1);
errout:
	XPluginDisable();
	return (0);
}

PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID from, int msg, void *param)
{
	UNUSED(from);
	UNUSED(msg);
	UNUSED(param);
}