/******************************************************************************
*   Copyright 2007-2009 by Aaron Seigo <aseigo@kde.org>                       *
*                                                                             *
*   This library is free software; you can redistribute it and/or             *
*   modify it under the terms of the GNU Library General Public               *
*   License as published by the Free Software Foundation; either              *
*   version 2 of the License, or (at your option) any later version.          *
*                                                                             *
*   This library is distributed in the hope that it will be useful,           *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          *
*   Library General Public License for more details.                          *
*                                                                             *
*   You should have received a copy of the GNU Library General Public License *
*   along with this library; see the file COPYING.LIB.  If not, write to      *
*   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,      *
*   Boston, MA 02110-1301, USA.                                               *
*******************************************************************************/

#include "plasmoidstructure.h"

#include <math.h>
#include <float.h> // FLT_MAX

#include <QCoreApplication>
#include <QFileInfo>

#include <kconfiggroup.h>
#include <kdesktopfile.h>
#include <klocalizedstring.h>

#include "package.h"
#include "config-package.h"

namespace KPackage
{

void PlasmoidPackage::initPackage(Package *package)
{
    GenericPackage::initPackage(package);
    package->setServicePrefix("plasma-applet-");
    package->setDefaultPackageRoot(PLASMA_RELATIVE_DATA_INSTALL_DIR "/plasmoids/");

    package->addFileDefinition("mainscript", "ui/main.qml", i18n("Main Script File"));
    package->addFileDefinition("configmodel", "config/config.qml", i18n("Configuration UI pages model"));
    package->addFileDefinition("mainconfigxml", "config/main.xml", i18n("Configuration XML file"));
}


} // namespace KPackage
