/******************************************************************************
*   Copyright 2011 by Aaron Seigo <aseigo@kde.org>                            *
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

#include "packagestructure.h"
#include "kpackage_debug.h"
#include <private/packagejob_p.h>
#include "private/package_p.h"

namespace KPackage
{

PackageStructure::PackageStructure(QObject *parent, const QVariantList &args)
    : QObject(parent),
      d(nullptr)
{
    Q_UNUSED(args)
}

PackageStructure::~PackageStructure()
{
}

void PackageStructure::initPackage(Package *package)
{
    Q_UNUSED(package)
}

void PackageStructure::pathChanged(Package *package)
{
    Q_UNUSED(package)
}

KJob *PackageStructure::install(Package *package, const QString &archivePath, const QString &packageRoot)
{
    PackageJob *j = new PackageJob(package);
    j->install(archivePath, packageRoot);
    return j;
}

KJob *PackageStructure::update(Package *package, const QString &archivePath, const QString &packageRoot)
{
    PackageJob *j = new PackageJob(package);
    j->update(archivePath, packageRoot);
    return j;
}

KJob *PackageStructure::uninstall(Package *package, const QString &packageRoot)
{
    PackageJob *j = new PackageJob(package);
    j->uninstall(packageRoot + QLatin1Char('/') + package->metadata().pluginId());
    return j;
}

}

#include "moc_packagestructure.cpp"
