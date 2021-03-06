/* This file is part of the KDE project
   Copyright ( C ) 2003 Nadeem Hasan <nhasan@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "MultiMeterSettings.h"
#include "ui_MultiMeterSettingsWidget.h"

#include <QColor>
#include <QDoubleValidator>

#include <KLocalizedString>

MultiMeterSettings::MultiMeterSettings(QWidget *parent, const QString &name )
    : QDialog( parent )
{
  setObjectName( name );
  setModal( true );
  setWindowTitle( i18nc( "Multimeter is a sensor display that mimics 'digital multimeter' aparatus", "Multimeter Settings" ) );

  QWidget *mainWidget = new QWidget( this );

  m_settingsWidget = new Ui_MultiMeterSettingsWidget;
  m_settingsWidget->setupUi( mainWidget );
  m_settingsWidget->m_lowerLimit->setValidator(new QDoubleValidator(m_settingsWidget->m_lowerLimit));
  m_settingsWidget->m_upperLimit->setValidator(new QDoubleValidator(m_settingsWidget->m_upperLimit));
  m_settingsWidget->m_title->setFocus();

  connect(m_settingsWidget->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_settingsWidget->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *vlayout = new QVBoxLayout(this);
  vlayout->addWidget(mainWidget);
  setLayout(vlayout);
}

MultiMeterSettings::~MultiMeterSettings()
{
  delete m_settingsWidget;
}

QString MultiMeterSettings::title() const
{
  return m_settingsWidget->m_title->text();
}

bool MultiMeterSettings::showUnit() const
{
  return m_settingsWidget->m_showUnit->isChecked();
}

bool MultiMeterSettings::lowerLimitActive() const
{
  return m_settingsWidget->m_lowerLimitActive->isChecked();
}

bool MultiMeterSettings::upperLimitActive() const
{
  return m_settingsWidget->m_upperLimitActive->isChecked();
}

double MultiMeterSettings::lowerLimit() const
{
  return m_settingsWidget->m_lowerLimit->text().toDouble();
}

double MultiMeterSettings::upperLimit() const
{
  return m_settingsWidget->m_upperLimit->text().toDouble();
}

QColor MultiMeterSettings::normalDigitColor() const
{
  return m_settingsWidget->m_normalDigitColor->color();
}

QColor MultiMeterSettings::alarmDigitColor()
{
  return m_settingsWidget->m_alarmDigitColor->color();
}

QColor MultiMeterSettings::meterBackgroundColor()
{
  return m_settingsWidget->m_backgroundColor->color();
}

void MultiMeterSettings::setTitle( const QString &title )
{
  m_settingsWidget->m_title->setText( title );
}

void MultiMeterSettings::setShowUnit( bool b )
{
  m_settingsWidget->m_showUnit->setChecked( b );
}

void MultiMeterSettings::setLowerLimitActive( bool b )
{
  m_settingsWidget->m_lowerLimitActive->setChecked( b );
}

void MultiMeterSettings::setUpperLimitActive( bool b )
{
  m_settingsWidget->m_upperLimitActive->setChecked( b );
}

void MultiMeterSettings::setLowerLimit( double limit )
{
  m_settingsWidget->m_lowerLimit->setText( QString::number( limit ) );
}

void MultiMeterSettings::setUpperLimit( double limit )
{
  m_settingsWidget->m_upperLimit->setText( QString::number( limit ) );
}

void MultiMeterSettings::setNormalDigitColor( const QColor &c )
{
  m_settingsWidget->m_normalDigitColor->setColor( c );
}

void MultiMeterSettings::setAlarmDigitColor( const QColor &c )
{
  m_settingsWidget->m_alarmDigitColor->setColor( c );
}

void MultiMeterSettings::setMeterBackgroundColor( const QColor &c )
{
  m_settingsWidget->m_backgroundColor->setColor( c );
}



/* vim: et sw=2 ts=2
*/

