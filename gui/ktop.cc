/*
    KTop, the KDE Task Manager and System Monitor
   
	Copyright (c) 1999 Chris Schlaeger <cs@kde.org>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	KTop is currently maintained by Chris Schlaeger <cs@kde.org>. Please do
	not commit any changes without consulting me first. Thanks!

	Early versions of ktop (<1.0) have been written by Bernd Johannes Wuebben
    <wuebben@math.cornell.edu> and Nicolas Leclercq <nicknet@planete.net>.
	While I tried to preserve their original ideas, KTop has been rewritten
    several times.

	$Id$
*/

#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#include <ktmainwindow.h>
#include <kconfig.h>
#include <klocale.h>
#include <kcmdlineargs.h>
#include <kmessagebox.h>
#include <kaboutdata.h>


#include "SensorBrowser.h"
#include "SensorManager.h"
#include "SensorAgent.h"
#include "Workspace.h"
#include "version.h"
#include "ktop.moc"



static const char *description = 
	I18N_NOOP("KDE process monitor");

#define KTOP_MIN_W	50
#define KTOP_MIN_H	50

/*
 * This is the constructor for the main widget. It sets up the menu and the
 * TaskMan widget.
 */
TopLevel::TopLevel(const char *name, int)
	: KTMainWindow(name)
{
	setCaption(i18n("KDE Task Manager"));

	// Create main menu
	menubar = new MainMenu(this, "MainMenu");
	CHECK_PTR(menubar);
	connect(menubar, SIGNAL(quit()), this, SLOT(quitSlot()));
	// register the menu bar with KTMainWindow
	setMenu(menubar);

	splitter = new QSplitter(this, "Splitter");
	CHECK_PTR(splitter);
	splitter->setOrientation(Horizontal);
	setView(splitter);

	sb = new SensorBrowser(splitter, SensorMgr, "SensorBrowser");
	CHECK_PTR(sb);

	ws = new Workspace(splitter, "Workspace");
	CHECK_PTR(ws);
	connect(menubar, SIGNAL(newWorkSheet()), ws, SLOT(newWorkSheet()));
	connect(menubar, SIGNAL(deleteWorkSheet()), ws, SLOT(deleteWorkSheet()));

	/* Create the status bar. It displays some information about the
	 * number of processes and the memory consumption of the local
	 * host. */
	statusbar = new KStatusBar(this, "statusbar");
	CHECK_PTR(statusbar);
	statusbar->insertItem(i18n("88888 Processes"), 0);
	statusbar->insertItem(i18n("Memory: 8888888 kB used, "
							   "8888888 kB free"), 1);
	statusbar->insertItem(i18n("Swap: 8888888 kB used, "
							   "8888888 kB free"), 2);
	setStatusBar(statusbar);

	localhost = SensorMgr->engage("localhost");
	/* Request info about the swapspace size and the units it is measured in.
	 * The requested info will be received by answerReceived(). */
	localhost->sendRequest("memswap?", (SensorClient*) this, 5);

	// call timerEvent to fill the status bar with real values
	timerEvent(0);

	/* Restore size of the dialog box that was used at end of last
	 * session.  Due to a bug in Qt we need to set the width to one
	 * more than the defined min width. If this is not done the widget
	 * is not drawn properly the first time. Subsequent redraws after
	 * resize are no problem.
	 *
	 * I need to implement a propper session management some day! */
	QString t = kapp->config()->readEntry(QString("G_Toplevel"));
	if(!t.isNull())
	{
		if (t.length() == 19)
		{ 
			int xpos, ypos, ww, wh;
			sscanf(t.data(), "%04d:%04d:%04d:%04d", &xpos, &ypos, &ww, &wh);
			setGeometry(xpos, ypos,
						ww <= KTOP_MIN_W ? KTOP_MIN_W + 1 : ww,
						wh <= KTOP_MIN_H ? KTOP_MIN_H : wh);
		}
	}

	setMinimumSize(sizeHint());

	readProperties(kapp->config());

	timerID = startTimer(2000);

	// show the dialog box
	show();
}

TopLevel::~TopLevel()
{
	killTimer(timerID);

	delete menubar;
	delete statusbar;
	delete splitter;
}

void 
TopLevel::quitSlot()
{
	saveProperties(kapp->config());

	kapp->config()->sync();
	qApp->quit();
}

void
TopLevel::timerEvent(QTimerEvent*)
{
	/* Request some info about the memory status. The requested information
	 * will be received by answerReceived(). */
	localhost->sendRequest("pscount", (SensorClient*) this, 0);
	localhost->sendRequest("memfree", (SensorClient*) this, 1);
	localhost->sendRequest("memused", (SensorClient*) this, 2);
	localhost->sendRequest("memswap", (SensorClient*) this, 3);
}

void
TopLevel::answerReceived(int id, const QString& answer)
{
	QString s;
	static QString unit;
	static long mUsed = 0;
	static long mFree = 0;
	static long sTotal = 0;
	static long sFree = 0;

	switch (id)
	{
	case 0:
		s = i18n("%1 Processes").arg(answer);
		statusbar->changeItem(s, 0);
		break;
	case 1:
		mFree = answer.toLong();
		break;
	case 2:
		mUsed = answer.toLong();
		s = i18n("Memory: %1 %2 used, %3 %4 free")
			.arg(mUsed).arg(unit).arg(mFree).arg(unit);
		statusbar->changeItem(s, 1);
		break;
	case 3:
		sFree = answer.toLong();
		s = i18n("Swap: %1 %2 used, %2 %4 free")
			.arg(sTotal - sFree).arg(unit).arg(sFree).arg(unit);
		statusbar->changeItem(s, 2);
		break;
	case 5:
		SensorIntegerInfo info(answer);
		sTotal = info.getMax();
		unit = info.getUnit();
		break;
	}
}


static const KCmdLineOptions options[] =
{
	{ "p <show>", I18N_NOOP("What to Show (list|perf)"), "list" },
	{ 0, 0, 0}
};


/*
 * Where it all begins.
 */
int
main(int argc, char** argv)
{
	KAboutData aboutData( "ktop", I18N_NOOP("KDE Task Manager"),
		KTOP_VERSION, description, KAboutData::License_GPL,
		"(c) 1997-2000, The KTop Developers");
	aboutData.addAuthor("Bernd Johannes Wuebben",0, "wuebben@math.cornell.edu");
	aboutData.addAuthor("Nicolas Leclercq",0, "nicknet@planete.net");
	aboutData.addAuthor("Chris Schlaeger","Current Maintainer", "cs@kde.org");
	
	KCmdLineArgs::init( argc, argv, &aboutData );
	KCmdLineArgs::addCmdLineOptions( options );
	
	// initialize KDE application
	KApplication a;

	int sfolder = -1;

	SensorMgr = new SensorManager();
	CHECK_PTR(SensorMgr);

	// create top-level widget
	TopLevel *toplevel = new TopLevel("TaskManager", sfolder);
	CHECK_PTR(toplevel);
	a.setMainWidget(toplevel);
	a.setTopWidget(toplevel);

//	Commented out so that y'all can start to get this to work properly
/*	{ // process command line arguments
		KCmdLineArgs *args = KCmdLineArgs::parsedArgs();
		if (tolower(args->getOption("p")[0])=='p')
			sfolder = TaskMan::PAGE_PLIST;
		else
			sfolder = TaskMan::PAGE_PLIST;
		
		args->clear();
    }
*/
    
	toplevel->show();

	// run the application
	int result = a.exec();

    delete toplevel;

	return (result);
}
