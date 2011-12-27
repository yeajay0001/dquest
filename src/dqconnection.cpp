#include <QtCore>
#include <QSqlDatabase>
#include <QCoreApplication>
#include <QSqlError>
#include <QMutex>
#include <QMap>

#include "dqmodel.h"
#include "dqconnection.h"
#include "priv/dqsqlitestatement.h"
#include "dqsql.h"
#include "priv/dqsqliteengine.h"

#define PREPARE_PRIV() {\
    if (isNull()) \
        d = new DQConnectionPriv(); \
    if (!d->engine) \
        setEngine(new DQSqliteEngine()); \
    }

/// The private database structure for DQConnection
class DQConnectionPriv : public QSharedData
{
  public:
    DQConnectionPriv() {
        engine = 0;
    }

    DQConnectionPriv(DQEngine *val) {
        engine = val;
    }

    ~DQConnectionPriv() {
        if (engine)
            delete engine;
    }

    DQSql m_sql;

    /// Registered modeles
//    QList<DQModelMetaInfo*> m_models;

    /// The last query being used.
    QSqlQuery lastQuery;

    QMutex mutex;

    DQEngine *engine;
};

/// The mapping of default connection
static QMap<DQModelMetaInfo* , DQConnection> mapping;

DQConnection::DQConnection()
{
//    d = new DQConnectionPriv();

    /* Don't create DQConnectionPriv and let the connection be null
     Create the content on depend.

     DQModel is going to change to not set default connection in constructor.
     A null connection will reduce the memory allocation */

}

DQConnection::DQConnection(const DQConnection& other) : d(other.d){
}

DQConnection & DQConnection::operator=(const DQConnection &rhs){
    if (this != &rhs)
        d.operator=(rhs.d);
    return *this;
}

DQConnection::~DQConnection(){
}

bool DQConnection::operator==(const DQConnection &rhs) {
    return d.constData() == rhs.d.constData();
}

bool DQConnection::operator!=(const DQConnection &rhs) {
    return d.constData() != rhs.d.constData();
}

bool DQConnection::open(QSqlDatabase db){
    Q_ASSERT(db.isOpen());

    PREPARE_PRIV();

    d->m_sql.setStatement(new DQSqliteStatement());
    d->m_sql.setDatabase(db);
    d->engine->open(db); /// @TODO : Change the return value to this function.

    return true;
}

bool DQConnection::isOpen() const{
    if (!d || !d->engine)
        return false;
    return d->engine->isOpen();
}

bool DQConnection::isNull(){
    return !d;
}

void DQConnection::close(){
    if (!d)
        return;
    d->m_sql.setDatabase(QSqlDatabase());
    d->engine->close();

    QList<DQModelMetaInfo*> models = d->engine->modelList();
    foreach (DQModelMetaInfo* metaInfo , models) {
        if (mapping.contains(metaInfo)) {
            mapping.take(metaInfo);
        }
    }
}

bool DQConnection::addModel(DQModelMetaInfo* metaInfo){
    bool res = false;
    if (!metaInfo) {
        return res;
    }

    PREPARE_PRIV();

    res = d->engine->addModel(metaInfo);

    if (res && !mapping.contains(metaInfo)) {
        // set the default connection
        mapping[metaInfo] = *this;
    }

    return res;
}

DQConnection DQConnection::defaultConnection(DQModelMetaInfo* metaInfo){
    DQConnection ret;
    if (!metaInfo)
        return ret;

    if (mapping.contains(metaInfo)) {
        ret = mapping[metaInfo];
    } else {
        qWarning() << QString("Model %1 is not added to any connection yet").arg(metaInfo->name());
    }

    return ret;
}

void DQConnection::setDefaultConnection(DQModelMetaInfo* metaInfo) {
    mapping[metaInfo] = *this;
}

bool DQConnection::createTables(){
    if (!isOpen())
        return false;

    bool res = true;

    QList<DQModelMetaInfo*> models = d->engine->modelList();
    foreach (DQModelMetaInfo* info ,models) {

        if (!d->m_sql.exists(info)) {

            if (!d->m_sql.createTableIfNotExists(info)){
                qWarning() << QString("DQConnection::createTables() - Failed to create table for %1 . Error : %2").arg(info->className())
                        .arg( d->m_sql.lastQuery().lastError().text());
                qWarning() << d->m_sql.lastQuery().lastQuery();
                res = false;
                setLastQuery( d->m_sql.lastQuery() );
                break;
            }

            /// @TODO Replace by list.save()
            DQSharedList initialData = info->initialData();
            int n = initialData.size();
            for (int i = 0 ; i< n;i++) {
                DQModel *model = static_cast<DQModel*>(initialData.at(i));
                model->setConnection(*this);
                model->save();
            }
        }
    }

    return res;
}

bool DQConnection::dropTables() {
    if (!isOpen())
        return false;

    bool res = true;

    QList<DQModelMetaInfo*> models = d->engine->modelList();

    foreach (DQModelMetaInfo* info ,models) {
        if (!d->m_sql.exists(info))
            continue;

        if (!d->m_sql.dropTable(info) ) {
            res = false;
            setLastQuery( d->m_sql.lastQuery() );
            break;
        }

    }

    return res;
}

bool DQConnection::createIndex(const DQBaseIndex &index) {
    if (!isOpen())
        return false;

    return d->m_sql.createIndexIfNotExists(index);
}

bool DQConnection::dropIndex(QString name){
    if (!isOpen())
        return false;

    return d->m_sql.dropIndexIfExists(name);
}

DQSql& DQConnection::sql(){
    return d->m_sql;
}

QSqlQuery DQConnection::query(){
    if (!isOpen())
        return QSqlQuery();

    return d->m_sql.query();
}

void DQConnection::setLastQuery(QSqlQuery query){
    if (!isOpen())
        return;

    d->mutex.lock();
    d->lastQuery = query;
    d->mutex.unlock();
}

QSqlQuery DQConnection::lastQuery(){
    if (!isOpen())
        return QSqlQuery();

    /*
      Although lastQuery() is thread-safe, but as it do not hold the
      lastQuery per thread. The result become meaningless , as it
      may override by another thread.
     @todo Implement last query storage per thread.
     */
    QSqlQuery query;
    d->mutex.lock();
    query = d->lastQuery;
    d->mutex.unlock();

    return query;
}

bool DQConnection::setEngine(DQEngine *engine){
    if (isOpen())
        return false;
    d->mutex.lock();

    if (d->engine) {
        delete d->engine;
        d->engine = 0;
    }
    d->engine = engine;
    d->mutex.unlock();
    return true;
}

DQEngine* DQConnection::engine() const{
    DQEngine*res = 0;

    d->mutex.lock();
    res = d->engine;
    d->mutex.unlock();
    return res;
}
