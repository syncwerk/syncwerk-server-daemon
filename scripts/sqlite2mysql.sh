#!/bin/sh
#
# This shell script and corresponding sqlite2mysql.py are used to
# migrate Syncwerk data from SQLite to MySQL.
#
# Setup:
# 
#  1. Move this file and sqlite2mysql.py to the top directory of your Syncwerk
#     installation path (e.g. /data/syncwerk).
#  2. Run: ./sqlite2mysql.sh
#  3. Three files(ccnet-db.sql, syncwerk-db.sql, restapi-db.sql) are created.
#  4. Loads these files to MySQL
#     (mysql> source ccnet-db.sql)
# 

CCNET_DB='ccnet-db.sql'
SYNCWERK_DB='syncwerk-db.sql'
RESTAPI_DB='restapi-db.sql'

########## ccnet
syncwerk_path=$(pwd)
if [ -f "${syncwerk_path}/ccnet/ccnet.conf" ]; then
    USER_MGR_DB=${syncwerk_path}/ccnet/PeerMgr/usermgr.db
    GRP_MGR_DB=${syncwerk_path}/ccnet/GroupMgr/groupmgr.db
else
    echo "${syncwerk_path}/ccnet/ccnet.conf does not exists."
    read -p "Please provide your ccnet.conf path(e.g. /data/syncwerk/ccnet/ccnet.conf): " ccnet_conf_path
    if [ -f ${ccnet_conf_path} ]; then
        USER_MGR_DB=$(dirname "${ccnet_conf_path}")/PeerMgr/usermgr.db
        GRP_MGR_DB=$(dirname "${ccnet_conf_path}")/GroupMgr/groupmgr.db
    else
        echo "${ccnet_conf_path} does not exists, quit."
        exit 1
    fi
fi

rm -rf ${CCNET_DB}

echo "sqlite3 ${USER_MGR_DB} .dump | python sqlite2mysql.py > ${CCNET_DB}"
sqlite3 ${USER_MGR_DB} .dump | python sqlite2mysql.py > ${CCNET_DB}
echo "sqlite3 ${GRP_MGR_DB} .dump | python sqlite2mysql.py >> ${CCNET_DB}"
sqlite3 ${GRP_MGR_DB} .dump | python sqlite2mysql.py >> ${CCNET_DB}

# change ctime from INTEGER to BIGINT in EmailUser table
sed 's/ctime INTEGER/ctime BIGINT/g' ${CCNET_DB} > ${CCNET_DB}.tmp && mv ${CCNET_DB}.tmp ${CCNET_DB}

# change email in UserRole from TEXT to VARCHAR(255)
sed 's/email TEXT, role TEXT/email VARCHAR(255), role TEXT/g' ${CCNET_DB} > ${CCNET_DB}.tmp && mv ${CCNET_DB}.tmp ${CCNET_DB}

########## syncwerk
rm -rf ${SYNCWERK_DB}

if [ -f "${syncwerk_path}/syncwerk-data/syncwerk.db" ]; then
    echo "sqlite3 ${syncwerk_path}/syncwerk-data/syncwerk.db .dump | python sqlite2mysql.py > ${SYNCWERK_DB}"
    sqlite3 ${syncwerk_path}/syncwerk-data/syncwerk.db .dump | python sqlite2mysql.py > ${SYNCWERK_DB}
else
    echo "${syncwerk_path}/syncwerk-data/syncwerk.db does not exists."
    read -p "Please provide your syncwerk.db path(e.g. /data/syncwerk/syncwerk-data/syncwerk.db): " syncwerk_db_path
    if [ -f ${syncwerk_db_path} ];then
        echo "sqlite3 ${syncwerk_db_path} .dump | python sqlite2mysql.py > ${SYNCWERK_DB}"
        sqlite3 ${syncwerk_db_path} .dump | python sqlite2mysql.py > ${SYNCWERK_DB}
    else
        echo "${syncwerk_db_path} does not exists, quit."
        exit 1
    fi
fi

# change owner_id in RepoOwner from TEXT to VARCHAR(255)
sed 's/owner_id TEXT/owner_id VARCHAR(255)/g' ${SYNCWERK_DB} > ${SYNCWERK_DB}.tmp && mv ${SYNCWERK_DB}.tmp ${SYNCWERK_DB}

# change user_name in RepoGroup from TEXT to VARCHAR(255)
sed 's/user_name TEXT/user_name VARCHAR(255)/g' ${SYNCWERK_DB} > ${SYNCWERK_DB}.tmp && mv ${SYNCWERK_DB}.tmp ${SYNCWERK_DB}

########## restapi
rm -rf ${RESTAPI_DB}

if [ -f "${syncwerk_path}/restapi.db" ]; then
    echo "sqlite3 ${syncwerk_path}/restapi.db .dump | tr -d '\n' | sed 's/;/;\n/g' | python sqlite2mysql.py > ${RESTAPI_DB}"
    sqlite3 ${syncwerk_path}/restapi.db .dump | tr -d '\n' | sed 's/;/;\n/g' | python sqlite2mysql.py > ${RESTAPI_DB}
else
    echo "${syncwerk_path}/restapi.db does not exists."
    read -p "Please prove your restapi.db path(e.g. /data/syncwerk/restapi.db): " restapi_db_path
    if [ -f ${restapi_db_path} ]; then
        echo "sqlite3 ${restapi_db_path} .dump | tr -d '\n' | sed 's/;/;\n/g' | python sqlite2mysql.py > ${RESTAPI_DB}"
        sqlite3 ${restapi_db_path} .dump | tr -d '\n' | sed 's/;/;\n/g' | python sqlite2mysql.py > ${RESTAPI_DB}
    else
        echo "${restapi_db_path} does not exists, quit."
        exit 1
    fi
fi

# change username from VARCHAR(256) to VARCHAR(255) in wiki_personalwiki
sed 's/varchar(256) NOT NULL UNIQUE/varchar(255) NOT NULL UNIQUE/g' ${RESTAPI_DB} > ${RESTAPI_DB}.tmp && mv ${RESTAPI_DB}.tmp ${RESTAPI_DB}

# remove unique from contacts_contact
sed 's/,    UNIQUE (`user_email`, `contact_email`)//g' ${RESTAPI_DB} > ${RESTAPI_DB}.tmp && mv ${RESTAPI_DB}.tmp ${RESTAPI_DB}

# remove base_dirfileslastmodifiedinfo records to avoid json string parsing issue between sqlite and mysql
sed '/INSERT INTO `base_dirfileslastmodifiedinfo`/d' ${RESTAPI_DB} > ${RESTAPI_DB}.tmp && mv ${RESTAPI_DB}.tmp ${RESTAPI_DB}

# remove notifications_usernotification records to avoid json string parsing issue between sqlite and mysql
sed '/INSERT INTO `notifications_usernotification`/d' ${RESTAPI_DB} > ${RESTAPI_DB}.tmp && mv ${RESTAPI_DB}.tmp ${RESTAPI_DB}


########## common logic

# add ENGIN=INNODB to create table statment
for sql_file in $CCNET_DB $SYNCWERK_DB $RESTAPI_DB
do
    sed -r 's/(CREATE TABLE.*);/\1 ENGINE=INNODB;/g' $sql_file > $sql_file.tmp && mv $sql_file.tmp $sql_file
done

# remove COLLATE NOCASE if possible
for sql_file in $CCNET_DB $SYNCWERK_DB $RESTAPI_DB
do
    sed 's/COLLATE NOCASE//g' $sql_file > $sql_file.tmp && mv $sql_file.tmp $sql_file
done

