/*
    KSysGuard, the KDE System Guard
   
    Copyright (c) 2003 Tobias Koenig <tokoe@kde.org>
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    KSysGuard is currently maintained by Chris Schlaeger <cs@kde.org>.
    Please do not commit any changes without consulting me first. Thanks!

    $Id$
*/

#ifndef KSG_TIMERSETTINGS_H
#define KSG_TIMERSETTINGS_H

#include <kdialogbase.h>

class QCheckBox;
class QLabel;
class QSpinBox;

class TimerSettings : public KDialogBase
{
  Q_OBJECT

  public:
    TimerSettings( QWidget *parent, const char *name = 0 );
    ~TimerSettings();

    void setUseGlobalUpdate( bool value );
    bool useGlobalUpdate() const;

    void setInterval( int interval );
    int interval() const;

  private slots:
    void globalUpdateChanged( bool );

  private:
    QCheckBox* mUseGlobalUpdate;
    QLabel* mLabel;
    QSpinBox* mInterval;
};

#endif