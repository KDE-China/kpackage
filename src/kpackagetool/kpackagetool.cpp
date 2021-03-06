/******************************************************************************
*   Copyright 2008 Aaron Seigo <aseigo@kde.org>                               *
*   Copyright 2012-2017 Sebastian Kügler <sebas@kde.org>                      *
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

#include "kpackagetool.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <kshell.h>
#include <klocalizedstring.h>
#include <kaboutdata.h>
#include <KPluginLoader>

#include <kpackage/packagestructure.h>
#include <kpackage/package.h>
#include <kpackage/packageloader.h>
#include <kjob.h>

#include <qcommandlineparser.h>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamWriter>

#include <iostream>
#include <iomanip>

#include "options.h"

#include "../kpackage/config-package.h"
//for the index creation function
#include "../kpackage/private/packagejobthread_p.h"

Q_GLOBAL_STATIC_WITH_ARGS(QTextStream, cout, (stdout))
Q_GLOBAL_STATIC_WITH_ARGS(QTextStream, cerr, (stderr))

static QVector<KPluginMetaData> listPackageTypes()
{
    QStringList libraryPaths;

    const QString subDirectory = QStringLiteral("kpackage/packagestructure");

    Q_FOREACH (const QString &dir, QCoreApplication::libraryPaths()) {
        QString d = dir + QDir::separator() + subDirectory;
        if (!d.endsWith(QDir::separator())) {
            d += QDir::separator();
        }
        libraryPaths << d;
    }

    QVector<KPluginMetaData> offers;
    Q_FOREACH (const QString &plugindir, libraryPaths) {
        const QString &_ixfile = plugindir + s_kpluginindex;
        QFile indexFile(_ixfile);
        if (indexFile.exists()) {
            indexFile.open(QIODevice::ReadOnly);
            QJsonDocument jdoc = QJsonDocument::fromBinaryData(indexFile.readAll());
            indexFile.close();

            QJsonArray plugins = jdoc.array();
            for (QJsonArray::const_iterator iter = plugins.constBegin(); iter != plugins.constEnd(); ++iter) {
                const QJsonObject obj = iter->toObject();
                const QString candidate = obj.value(QStringLiteral("FileName")).toString();
                offers << KPluginMetaData(obj, candidate);
            }
        } else {
            QVector<KPluginMetaData> plugins = KPluginLoader::findPlugins(plugindir);
            QVectorIterator<KPluginMetaData> iter(plugins);
            while (iter.hasNext()) {
                auto md = iter.next();
                offers << md;
            }
        }
    }
    return offers;
}

namespace KPackage
{
class PackageToolPrivate
{
public:
    QString packageRoot;
    QString packageFile;
    QString package;
    QStringList pluginTypes;
    KPackage::Package installer;
    KPluginMetaData metadata;
    QString installPath;
    void output(const QString &msg);
    QStringList packages(const QStringList &types, const QString &path = QString());
    void renderTypeTable(const QMap<QString, QStringList> &plugins);
    void listTypes();
    void coutput(const QString &msg);
    void cerror(const QString &msg);
    QCommandLineParser *parser;
};

PackageTool::PackageTool(int &argc, char **argv, QCommandLineParser *parser) :
    QCoreApplication(argc, argv)
{
    d = new PackageToolPrivate;
    d->parser = parser;
    QTimer::singleShot(0, this, SLOT(runMain()));
}

PackageTool::~PackageTool()
{
    delete d;
}

void PackageTool::runMain()
{
    KPackage::PackageStructure structure;
    if (d->parser->isSet(Options::hash)) {
        const QString path = d->parser->value(Options::hash);
        KPackage::Package package(&structure);
        package.setPath(path);
        const QString hash =
            QString::fromLocal8Bit(package.cryptographicHash(QCryptographicHash::Sha1));
        if (hash.isEmpty()) {
            d->coutput(i18n("Failed to generate a Package hash for %1", path));
            exit(9);
        } else {
            d->coutput(i18n("SHA1 hash for Package at %1: '%2'", package.path(), hash));
            exit(0);
        }
        return;
    }

    if (d->parser->isSet(Options::listTypes)) {
        d->listTypes();
        exit(0);
        return;
    }

    QString type = d->parser->value(Options::type);
    d->pluginTypes.clear();
    d->installer = Package();

    if (d->parser->isSet(Options::remove)) {
        d->package = d->parser->value(Options::remove);
    } else if (d->parser->isSet(Options::upgrade)) {
        d->package = d->parser->value(Options::upgrade);
    } else if (d->parser->isSet(Options::install)) {
        d->package = d->parser->value(Options::install);
    } else if (d->parser->isSet(Options::show)) {
        d->package = d->parser->value(Options::show);
    } else if (d->parser->isSet(Options::appstream)) {
        d->package = d->parser->value(Options::appstream);
    }

    if (!QDir::isAbsolutePath(d->package)) {
        d->packageFile = QDir(QDir::currentPath() + QLatin1Char('/') + d->package).absolutePath();
        d->packageFile = QFileInfo(d->packageFile).canonicalFilePath();
        if (d->parser->isSet(Options::upgrade)) {
            d->package = d->packageFile;
        }
    } else {
        d->packageFile = d->package;
    }

    if (!d->packageFile.isEmpty() && (!d->parser->isSet(Options::type) ||
                                      type.compare(i18nc("package type", "wallpaper"), Qt::CaseInsensitive) == 0 ||
                                      type.compare(QLatin1String("wallpaper"), Qt::CaseInsensitive) == 0)) {
        // Check type for common plasma packages
        KPackage::Package package(&structure);
        QString serviceType;
        package.setPath(d->packageFile);

        if (package.isValid() && package.metadata().isValid()) {
            serviceType = package.metadata().value(QStringLiteral("X-Plasma-ServiceType"));
            if (serviceType.isEmpty() && !package.metadata().serviceTypes().isEmpty()) {
                serviceType = package.metadata().serviceTypes().first();
            }
        }

        if (!serviceType.isEmpty()) {
            if (serviceType == QLatin1String("KPackage/Generic")) {
                type = QStringLiteral("KPackage/Generic");
            } else {
                type = serviceType;
                //qDebug() << "fallthrough type is" << serviceType;
            }
        }
    }

    {
        PackageStructure *structure = PackageLoader::self()->loadPackageStructure(type);

        if (structure) {
            d->installer = Package(structure);
        }

        if (!d->installer.hasValidStructure()) {
            qWarning() << "Package type" << type << "not found";
        }

        d->packageRoot = d->installer.defaultPackageRoot();
        d->pluginTypes << type;
    }
    if (d->parser->isSet(Options::show)) {
        const QString pluginName = d->package;
        showPackageInfo(pluginName);
        return;
    } else if (d->parser->isSet(Options::appstream)) {
        const QString pluginName = d->package;
        showAppstreamInfo(pluginName);
        return;
    }

    if (d->parser->isSet(Options::list)) {
        d->packageRoot = findPackageRoot(d->package, d->packageRoot);
        d->coutput(i18n("Listing service types: %1 in %2", d->pluginTypes.join(QStringLiteral(", ")), d->packageRoot));
        listPackages(d->pluginTypes, d->packageRoot);
        exit(0);

    } else if (d->parser->isSet(Options::generateIndex)) {
        recreateIndex();
        exit(0);

    } else if (d->parser->isSet(Options::removeIndex)) {
        removeIndex();
        exit(0);

    } else {
        // install, remove or upgrade
        if (!d->installer.isValid()) {

            d->installer = KPackage::Package(new KPackage::PackageStructure());
        }

        d->packageRoot = findPackageRoot(d->package, d->packageRoot);

        if (d->parser->isSet(Options::remove) || d->parser->isSet(Options::upgrade)) {
            QString pkgPath;
            foreach (const QString &t, d->pluginTypes) {
                KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(t);
                pkg.setPath(d->package);
                if (pkg.isValid()) {
                    pkgPath = pkg.path();
                    if (pkgPath.isEmpty() && !d->packageFile.isEmpty()) {
                        pkgPath = d->packageFile;
                    }
                    continue;
                }
            }
            if (pkgPath.isEmpty()) {
                pkgPath = d->package;
            }

            if (d->parser->isSet(Options::upgrade)) {
                d->installer.setPath(d->package);
            }
            QString _p = d->packageRoot;
            if (!_p.endsWith(QLatin1Char('/'))) {
                _p.append(QLatin1Char('/'));
            }
            _p.append(d->package);
            d->installer.setDefaultPackageRoot(d->packageRoot);
            d->installer.setPath(pkgPath);

            if (!d->parser->isSet(Options::type)) {
                foreach (const QString &st, d->installer.metadata().serviceTypes()) {
                    if (!d->pluginTypes.contains(st)) {
                        d->pluginTypes << st;
                    }
                }
            }

            QString pluginName;
            if (d->installer.isValid()) {
                d->metadata = d->installer.metadata();
                if (!d->metadata.isValid()) {
                    pluginName = d->package;
                } else if (!d->metadata.isValid() && d->metadata.pluginId().isEmpty()) {
                    // plugin name given in command line
                    pluginName = d->package;
                } else {
                    // Parameter was a plasma package, get plugin name from the package
                    pluginName = d->metadata.pluginId();
                }
            }
            QStringList installed = d->packages(d->pluginTypes);

            if (QFile::exists(d->packageFile)) {
                d->installer.setPath(d->packageFile);
                if (d->installer.isValid()) {
                    if (d->installer.metadata().isValid()) {
                        pluginName = d->installer.metadata().pluginId();
                    }
                }
            }
            // Uninstalling ...
            if (installed.contains(pluginName)) { // Assume it's a plugin name
                d->installer.setPath(pluginName);
                KJob *uninstallJob = d->installer.uninstall(pluginName, d->packageRoot);
                connect(uninstallJob, SIGNAL(result(KJob*)), SLOT(packageUninstalled(KJob*)));
                return;
            } else {
                d->coutput(i18n("Error: Plugin %1 is not installed.", pluginName));
                exit(2);
            }
        }
        if (d->parser->isSet(Options::install)) {
            KJob *installJob = d->installer.install(d->packageFile, d->packageRoot);
            connect(installJob, SIGNAL(result(KJob*)), SLOT(packageInstalled(KJob*)));
            return;
        }
        if (d->package.isEmpty()) {
            qWarning() << i18nc("No option was given, this is the error message telling the user he needs at least one, do not translate install, remove, upgrade nor list", "One of install, remove, upgrade or list is required.");
            exit(6);
        }
    }
}

void PackageToolPrivate::coutput(const QString &msg)
{
    *cout << msg << endl;
}

void PackageToolPrivate::cerror(const QString &msg)
{
    *cerr << msg << endl;
}

QStringList PackageToolPrivate::packages(const QStringList &types, const QString &path)
{
    QStringList result;

    foreach (const QString &type, types) {
        const QList<KPluginMetaData> services = KPackage::PackageLoader::self()->listPackages(type, path);
        foreach (const KPluginMetaData &service, services) {
            const QString _plugin = service.pluginId();
            if (!result.contains(_plugin)) {
                result << _plugin;
            }
        }
    }

    return result;
}

void PackageTool::showPackageInfo(const QString &pluginName)
{
    QString type = QStringLiteral("KPackage/Generic");
    if (!d->pluginTypes.contains(type) && d->pluginTypes.count() > 0) {
        type = d->pluginTypes.at(0);
    }
    KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(type);

    pkg.setDefaultPackageRoot(d->packageRoot);

    if (QFile::exists(d->packageFile)) {
        pkg.setPath(d->packageFile);
    } else {
        pkg.setPath(pluginName);
    }

    KPluginMetaData i = pkg.metadata();
    if (!i.isValid()) {
        *cerr << i18n("Error: Can't find plugin metadata: %1\n", pluginName);
        exit(3);
        return;
    }
    d->coutput(i18n("Showing info for package: %1", pluginName));
    d->coutput(i18n("      Name : %1", i.name()));
    d->coutput(i18n("   Comment : %1", i.value(QStringLiteral("Comment"))));
    d->coutput(i18n("    Plugin : %1", i.pluginId()));
    auto const authors = i.authors();
    d->coutput(i18n("    Author : %1", authors.first().name()));
    d->coutput(i18n("      Path : %1", pkg.path()));

    exit(0);
}

bool translateKPluginToAppstream(const QString &tagName, const QString &configField, const QJsonObject &configObject, QXmlStreamWriter& writer, bool canEndWithDot)
{
    const QRegularExpression rx(QStringLiteral("%1\\[(.*)\\]").arg(configField));
    const QJsonValue native = configObject.value(configField);
    if (native.isUndefined()) {
        return false;
    }

    QString content = native.toString();
    if (!canEndWithDot && content.endsWith(QLatin1Char('.'))) {
        content.chop(1);
    }
    writer.writeTextElement(tagName, content);
    for (auto it = configObject.begin(), itEnd = configObject.end(); it != itEnd; ++it) {
        const auto match = rx.match(it.key());

        if (match.hasMatch()) {
            QString content = it->toString();
            if (!canEndWithDot && content.endsWith(QLatin1Char('.'))) {
                content.chop(1);
            }

            writer.writeStartElement(tagName);
            writer.writeAttribute(QStringLiteral("xml:lang"), match.captured(1));
            writer.writeCharacters(content);
            writer.writeEndElement();
        }
    }
    return true;
}

void PackageTool::showAppstreamInfo(const QString &pluginName)
{
    KPluginMetaData i;
    KPluginMetaData packageStructureMetaData;
    //if the path passed is an absolute path, and a metadata file is found under it, use that metadata file to generate the appstream info.
    // This can happen in the case an application wanting to support kpackage based extensions includes in the same project both the packagestructure plugin and the packages themselves. In that case at build time the packagestructure plugin wouldn't be installed yet

    if (QFile::exists(pluginName + QStringLiteral("/metadata.desktop"))) {
        i = KPluginMetaData(pluginName + QStringLiteral("/metadata.desktop"));
    } else if (QFile::exists(pluginName + QStringLiteral("/metadata.json"))) {
        i = KPluginMetaData(pluginName + QStringLiteral("/metadata.json"));
    } else {
        QString type = QStringLiteral("KPackage/Generic");
        if (!d->pluginTypes.contains(type) && d->pluginTypes.count() > 0) {
            type = d->pluginTypes.at(0);
        }
        KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(type);

        pkg.setDefaultPackageRoot(d->packageRoot);

        if (QFile::exists(d->packageFile)) {
            pkg.setPath(d->packageFile);
        } else {
            pkg.setPath(pluginName);
        }

        {
            foreach(const KPluginMetaData &md, listPackageTypes()) {
                if (md.pluginId() == type) {
                    packageStructureMetaData = md;
                    break;
                }
            }
        }

        i = pkg.metadata();
    }

    if (!i.isValid()) {
        *cerr << i18n("Error: Can't find plugin metadata: %1\n", pluginName);
        std::exit(3);
        return;
    }

    QString parentApp = i.rawData().value(QLatin1String("X-KDE-ParentApp")).toString();
    if (parentApp.isEmpty()) {
        parentApp = packageStructureMetaData.rawData().value(QLatin1String("X-KDE-ParentApp")).toString();
    }
    const QJsonObject rootObject = i.rawData()[QStringLiteral("KPlugin")].toObject();

    // Be super aggressive in finding a NoDisplay property. It can be a top-level property or
    // inside the KPlugin object, it also can be either a stringy type or a bool type. Try all
    // possible scopes and type conversions to find NoDisplay
    for (const auto object : { i.rawData(), rootObject }) {
        if (object.value(QLatin1String("NoDisplay")).toBool(false) ||
                // Standard value is unprocessed string we'll need to deal with.
                object.value(QLatin1String("NoDisplay")).toString() == QStringLiteral("true")) {
            std::exit(0);
        }
    }

    QXmlStreamAttributes componentAttributes;
    if (!parentApp.isEmpty()) {
        componentAttributes << QXmlStreamAttribute(QLatin1String("type"), QLatin1String("addon"));
    }

    // Compatibility: without appstream-metainfo-output argument we print the XML output to STDOUT
    // with the argument we'll print to the defined path.
    // TODO: in KF6 we should switch to argument-only.
    QIODevice *outputDevice = cout->device();
    QScopedPointer<QFile> outputFile;
    const auto outputPath = d->parser->value(Options::appstreamOutput);
    if (!outputPath.isEmpty()) {
        auto outputUrl = QUrl::fromUserInput(outputPath);
        outputFile.reset(new QFile(outputUrl.toLocalFile()));
        if (!outputFile->open(QFile::WriteOnly | QFile::Text)) {
            *cerr << "Failed to open output file for writing.";
            exit(1);
        }
        outputDevice = outputFile.data();
    }

    QXmlStreamWriter writer(outputDevice);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("component"));
    writer.writeAttributes(componentAttributes);

    writer.writeTextElement(QStringLiteral("id"), i.pluginId());
    if (!parentApp.isEmpty()) {
        writer.writeTextElement(QStringLiteral("extends"), parentApp);
    }
    translateKPluginToAppstream(QStringLiteral("name"), QStringLiteral("Name"), rootObject, writer, false);
    translateKPluginToAppstream(QStringLiteral("summary"), QStringLiteral("Description"), rootObject, writer, false);
    if (!i.website().isEmpty()) {
        writer.writeStartElement(QStringLiteral("url"));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("homepage"));
        writer.writeCharacters(i.website());
        writer.writeEndElement();
    }

    if (i.pluginId().startsWith(QLatin1String("org.kde."))) {
        writer.writeStartElement(QStringLiteral("url"));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("donation"));
        writer.writeCharacters(QStringLiteral("https://www.kde.org/donate.php?app=%1").arg(i.pluginId()));
        writer.writeEndElement();
    }

    const auto authors = i.authors();
    if (!authors.isEmpty()) {
        QStringList authorsText;
        for (const auto &author: authors) {
            authorsText += QStringLiteral("%1 <%2>").arg(author.name(), author.emailAddress());
        }
        writer.writeTextElement(QStringLiteral("developer_name"), authorsText.join(QStringLiteral(", ")));
    }

    if (!i.iconName().isEmpty()) {
        writer.writeStartElement(QStringLiteral("icon"));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("stock"));
        writer.writeCharacters(i.iconName());
        writer.writeEndElement();
    }
    writer.writeTextElement(QStringLiteral("project_license"), KAboutLicense::byKeyword(i.license()).spdx());
    writer.writeTextElement(QStringLiteral("metadata_license"), QStringLiteral("CC0-1.0"));
    writer.writeEndElement();
    writer.writeEndDocument();

    exit(0);
}

QString PackageTool::findPackageRoot(const QString &pluginName, const QString &prefix)
{
    Q_UNUSED(pluginName);
    Q_UNUSED(prefix);
    QString packageRoot;
    if (d->parser->isSet(Options::packageRoot) && d->parser->isSet(Options::global) && !d->parser->isSet(Options::generateIndex)) {
        qWarning() << i18nc("The user entered conflicting options packageroot and global, this is the error message telling the user he can use only one", "The packageroot and global options conflict with each other, please select only one.");
        ::exit(7);
    } else if (d->parser->isSet(Options::packageRoot)) {
        packageRoot = d->parser->value(Options::packageRoot);
        //qDebug() << "(set via arg) d->packageRoot is: " << d->packageRoot;
    } else if (d->parser->isSet(Options::global)) {
        auto const paths = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, d->packageRoot, QStandardPaths::LocateDirectory);
        if (!paths.isEmpty()) {
            packageRoot = paths.last();
        }
    } else {
        packageRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + d->packageRoot;
    }
    return packageRoot;
}

void PackageTool::listPackages(const QStringList &types, const QString &path)
{
    QStringList list = d->packages(types, path);
    list.sort();
    foreach (const QString &package, list) {
        d->coutput(package);
    }
    exit(0);
}

void PackageToolPrivate::renderTypeTable(const QMap<QString, QStringList> &plugins)
{
    const QString nameHeader = i18n("Addon Name");
    const QString pluginHeader = i18n("Service Type");
    const QString pathHeader = i18n("Path");
    int nameWidth = nameHeader.length();
    int pluginWidth = pluginHeader.length();
    int pathWidth = pathHeader.length();

    QMapIterator<QString, QStringList> pluginIt(plugins);
    while (pluginIt.hasNext()) {
        pluginIt.next();
        if (pluginIt.key().length() > nameWidth) {
            nameWidth = pluginIt.key().length();
        }

        if (pluginIt.value()[0].length() > pluginWidth) {
            pluginWidth = pluginIt.value()[0].length();
        }

        if (pluginIt.value()[1].length() > pathWidth) {
            pathWidth = pluginIt.value()[1].length();
        }

    }

    std::cout << nameHeader.toLocal8Bit().constData() << std::setw(nameWidth - nameHeader.length() + 2) << ' '
              << pluginHeader.toLocal8Bit().constData() << std::setw(pluginWidth - pluginHeader.length() + 2) << ' '
              << pathHeader.toLocal8Bit().constData() << std::setw(pathWidth - pathHeader.length() + 2) << ' '
              << std::endl;
    std::cout << std::setfill('-') << std::setw(nameWidth) << '-' << "  "
              << std::setw(pluginWidth) << '-' << "  "
              << std::setw(pathWidth) << '-' << "  "
              << std::endl;
    std::cout << std::setfill(' ');

    pluginIt.toFront();
    while (pluginIt.hasNext()) {
        pluginIt.next();
        std::cout << pluginIt.key().toLocal8Bit().constData() << std::setw(nameWidth - pluginIt.key().length() + 2) << ' '
                  << pluginIt.value()[0].toLocal8Bit().constData() << std::setw(pluginWidth - pluginIt.value()[0].length() + 2) << ' '
                  << pluginIt.value()[1].toLocal8Bit().constData() << std::setw(pathWidth - pluginIt.value()[1].length() + 2) << std::endl;
    }
}

void PackageToolPrivate::listTypes()
{
    coutput(i18n("Package types that are installable with this tool:"));
    coutput(i18n("Built in:"));

    QMap<QString, QStringList> builtIns;
    builtIns.insert(i18n("KPackage/Generic"), QStringList() << QStringLiteral("KPackage/Generic") << QStringLiteral(KPACKAGE_RELATIVE_DATA_INSTALL_DIR "/packages/"));
    builtIns.insert(i18n("KPackage/GenericQML"), QStringList() << QStringLiteral("KPackage/GenericQML") << QStringLiteral(KPACKAGE_RELATIVE_DATA_INSTALL_DIR "/genericqml/"));

    renderTypeTable(builtIns);

    const QVector<KPluginMetaData> offers = listPackageTypes();

    if (!offers.isEmpty()) {
        std::cout << std::endl;
        coutput(i18n("Provided by plugins:"));

        QMap<QString, QStringList> plugins;
        foreach (const KPluginMetaData &info, offers) {
            KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(info.pluginId());
            QString name = info.name();
            QString plugin = info.pluginId();
            QString path = pkg.defaultPackageRoot();
            plugins.insert(name, QStringList() << plugin << path);
        }

        renderTypeTable(plugins);
    }
}

void PackageTool::recreateIndex()
{
    d->packageRoot = findPackageRoot(d->package, d->packageRoot);

    if (!QDir::isAbsolutePath(d->packageRoot)) {
        if (d->parser->isSet(Options::global)) {

            // Find package roots
            QStringList packageRoots;
            const QVector<KPluginMetaData> offers = listPackageTypes();
            if (!offers.isEmpty()) {
                foreach (const KPluginMetaData &info, offers) {
                    KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(info.pluginId());
                    const auto proot = pkg.defaultPackageRoot();
                    if (!proot.isEmpty()) {
                        packageRoots << pkg.defaultPackageRoot();
                    }
                }
            }

            Q_FOREACH(auto const &p, QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, d->packageRoot, QStandardPaths::LocateDirectory)) {
                for (const auto &_proot: packageRoots) {
                    QFileInfo packageDirInfo(QStringLiteral("%1%2").arg(p, _proot));
                    if (!packageDirInfo.isWritable()) {
                        continue;
                    }
                    const QString packagedir = packageDirInfo.absoluteFilePath();
                    if (KPackage::indexDirectory(packagedir, s_kpluginindex)) {
                        d->coutput(i18n("Generating %1%2", packagedir, s_kpluginindex));
                    } else {
                        d->cerror(i18n("Didn't write %1%2", packagedir, s_kpluginindex));
                    }
                }
            }
            return;
        } else {
            d->packageRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + d->packageRoot;
        }
    }

    if (KPackage::indexDirectory(d->packageRoot, s_kpluginindex)) {
        d->coutput(i18n("Generating %1/%2", d->packageRoot, s_kpluginindex));
    } else {
        d->cerror(i18n("Cannot write %1/%2", d->packageRoot, s_kpluginindex));
    }
}

void PackageTool::removeIndex()
{
    d->packageRoot = findPackageRoot(d->package, d->packageRoot);

    if (!QDir::isAbsolutePath(d->packageRoot)) {
        if (d->parser->isSet(Options::global)) {
            Q_FOREACH(auto const &p, QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, d->packageRoot, QStandardPaths::LocateDirectory)) {
                QDirIterator it(p, QStringList(s_kpluginindex), QDir::Files | QDir::Writable | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                qDebug() << "IX index remove" << p;
                while (it.hasNext()) {
                    it.next();
                    const QString path = it.fileInfo().absoluteFilePath();
                    QFile file(path);
                    if (!file.remove()) {
                        d->cerror(i18n("Could not remove index file %1", file.fileName()));
                    } else {
                        d->coutput(i18n("Removed %1", file.fileName()));
                    }
                }
            }
            return;
        } else {
            d->packageRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + d->packageRoot;
        }
    }
    QFile file(d->packageRoot + s_kpluginindex);
    if (file.exists()) {
        if (!file.remove()) {
            d->cerror(i18n("Could not remove index file %1", file.fileName()));
        } else {
            d->coutput(i18n("Removed %1", file.fileName()));
        }
    }
}

void PackageTool::packageInstalled(KJob *job)
{
    bool success = (job->error() == KJob::NoError);
    int exitcode = 0;
    if (success) {
        if (d->parser->isSet(Options::upgrade)) {
            d->coutput(i18n("Successfully upgraded %1", d->packageFile));
        } else {
            d->coutput(i18n("Successfully installed %1", d->packageFile));
        }
    } else {
        d->cerror(i18n("Error: Installation of %1 failed: %2", d->packageFile, job->errorText()));
        exitcode = 4;
    }
    exit(exitcode);
}

void PackageTool::packageUninstalled(KJob *job)
{
    bool success = (job->error() == KJob::NoError);
    int exitcode = 0;
    if (success) {
        if (d->parser->isSet(Options::upgrade)) {
            d->coutput(i18n("Upgrading package from file: %1", d->packageFile));
            KJob *installJob = d->installer.install(d->packageFile, d->packageRoot);
            connect(installJob, SIGNAL(result(KJob*)), SLOT(packageInstalled(KJob*)));
            return;
        }
        d->coutput(i18n("Successfully uninstalled %1", d->packageFile));
    } else {
        d->cerror(i18n("Error: Uninstallation of %1 failed: %2", d->packageFile, job->errorText()));
        exitcode = 7;
    }
    exit(exitcode);
}

} // namespace KPackage

#include "moc_kpackagetool.cpp"
