#include "RoadView.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QString());
    app.setOrganizationDomain(QStringLiteral("horizonroads.plm"));
    app.setApplicationName(QStringLiteral("horizonroads.plm"));

    qmlRegisterType<RoadView>("HorizonRoads", 1, 0, "RoadViewport");

    QQmlApplicationEngine engine;
    const QString binDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        binDir + QStringLiteral("/../../../qml/Main.qml"),
        binDir + QStringLiteral("/qml/Main.qml"),
        QDir::currentPath() + QStringLiteral("/qml/Main.qml"),
        QDir::currentPath() + QStringLiteral("/click/qml/Main.qml")
    };

    QUrl entryUrl;
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            entryUrl = QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath());
            break;
        }
    }
    if (entryUrl.isEmpty()) {
        qFatal("Unable to locate qml/Main.qml");
        return 1;
    }

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [entryUrl](QObject* obj, const QUrl& objUrl) {
                         if (!obj && entryUrl == objUrl) QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(entryUrl);
    return app.exec();
}
