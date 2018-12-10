CREATE TABLE IF NOT EXISTS SyncwerkConf (cfg_group VARCHAR(255) NOT NULL, cfg_key VARCHAR(255) NOT NULL, value VARCHAR(255), property INTEGER) ENGINE=INNODB;

CREATE TABLE IF NOT EXISTS RepoInfo (repo_id CHAR(36) PRIMARY KEY, name VARCHAR(255) NOT NULL, update_time BIGINT, version INTEGER, is_encrypted INTEGER, last_modifier VARCHAR(255)) ENGINE=INNODB;

ALTER TABLE Repo DROP primary key;
ALTER TABLE Repo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE Repo ADD UNIQUE (repo_id);

ALTER TABLE RepoOwner DROP primary key;
ALTER TABLE RepoOwner ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoOwner ADD UNIQUE (repo_id);

ALTER TABLE RepoGroup ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;

ALTER TABLE InnerPubRepo DROP primary key;
ALTER TABLE InnerPubRepo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE InnerPubRepo ADD UNIQUE (repo_id);

ALTER TABLE RepoUserToken ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;

ALTER TABLE RepoTokenPeerInfo DROP primary key;
ALTER TABLE RepoTokenPeerInfo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoTokenPeerInfo ADD UNIQUE (token);

ALTER TABLE RepoHead DROP primary key;
ALTER TABLE RepoHead ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoHead ADD UNIQUE (repo_id);

ALTER TABLE RepoSize DROP primary key;
ALTER TABLE RepoSize ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoSize ADD UNIQUE (repo_id);

ALTER TABLE RepoHistoryLimit DROP primary key;
ALTER TABLE RepoHistoryLimit ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoHistoryLimit ADD UNIQUE (repo_id);

ALTER TABLE RepoValidSince DROP primary key;
ALTER TABLE RepoValidSince ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoValidSince ADD UNIQUE (repo_id);

ALTER TABLE WebAP DROP primary key;
ALTER TABLE WebAP ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE WebAP ADD UNIQUE (repo_id);

ALTER TABLE VirtualRepo DROP primary key;
ALTER TABLE VirtualRepo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE VirtualRepo ADD UNIQUE (repo_id);

ALTER TABLE GarbageRepos DROP primary key;
ALTER TABLE GarbageRepos ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE GarbageRepos ADD UNIQUE (repo_id);

ALTER TABLE RepoTrash DROP primary key;
ALTER TABLE RepoTrash ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoTrash ADD UNIQUE (repo_id);

ALTER TABLE RepoFileCount DROP primary key;
ALTER TABLE RepoFileCount ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoFileCount ADD UNIQUE (repo_id);

ALTER TABLE RepoInfo DROP primary key;
ALTER TABLE RepoInfo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE RepoInfo ADD UNIQUE (repo_id);

ALTER TABLE UserQuota DROP primary key;
ALTER TABLE UserQuota ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE UserQuota ADD UNIQUE (user);

ALTER TABLE UserShareQuota DROP primary key;
ALTER TABLE UserShareQuota ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE UserShareQuota ADD UNIQUE (user);

ALTER TABLE OrgQuota DROP primary key;
ALTER TABLE OrgQuota ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE OrgQuota ADD UNIQUE (org_id);

ALTER TABLE OrgUserQuota DROP primary key;
ALTER TABLE OrgUserQuota ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE OrgUserQuota ADD UNIQUE (org_id, user);

ALTER TABLE SystemInfo ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;

ALTER TABLE Branch DROP primary key;
ALTER TABLE Branch ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
ALTER TABLE Branch ADD UNIQUE (repo_id, name);

ALTER TABLE SyncwerkConf ADD id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;