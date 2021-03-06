/******************************************************************************
*   Copyright 2012 Sebastian Kügler <sebas@kde.org>                           *
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

#include "packagejob_p.h"
#include "packagejobthread_p.h"
#include "config-package.h"

#include "package_p.h"

#include "kpackage_debug.h"

namespace KPackage
{
class PackageJobPrivate
{
public:
    PackageJobThread *thread;
    Package *package;
    QString installPath;
};

PackageJob::PackageJob(Package *package, QObject *parent) :
    KJob(parent)
{
    d = new PackageJobPrivate;
    d->thread = new PackageJobThread(this);
    d->package = package;

    connect(PackageDeletionNotifier::self(), &PackageDeletionNotifier::packageDeleted, this, [this](Package *package) {
        if (package == d->package) {
            d->package = nullptr;
        }
    });

    connect(d->thread, SIGNAL(finished(bool,QString)),
            SLOT(slotFinished(bool,QString)), Qt::QueuedConnection);
    connect(d->thread, &PackageJobThread::installPathChanged, this,
            [this](const QString &installPath) {
                if (d->package) {
                    d->package->setPath(installPath);
                }
                emit installPathChanged(installPath);
            }, Qt::QueuedConnection);
}

PackageJob::~PackageJob()
{
    delete d;
}

void PackageJob::slotFinished(bool ok, const QString &err)
{
    if (ok) {
        setError(NoError);
    } else {
        setError(d->thread->errorCode());
        setErrorText(err);
    }
    d->thread->exit(0);
    emitResult();
}

void PackageJob::start()
{
    d->thread->start();
}

void PackageJob::install(const QString &src, const QString &dest)
{
    d->thread->install(src, dest);
}

void PackageJob::update(const QString &src, const QString &dest)
{
    d->thread->update(src, dest);
}

void PackageJob::uninstall(const QString &installationPath)
{
    d->thread->uninstall(installationPath);
}

} // namespace KPackage

#include "moc_packagejob_p.cpp"
