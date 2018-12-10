import os
import random
import string

from synserv import ccnet_api, syncwerk_api


def create_and_get_repo(*a, **kw):
    repo_id = syncwerk_api.create_repo(*a, **kw)
    repo = syncwerk_api.get_repo(repo_id)
    return repo


def randstring(length=12):
    return ''.join(random.choice(string.lowercase) for i in range(length))

def create_and_get_group(*a, **kw):
    group_id = ccnet_api.create_group(*a, **kw)
    group = ccnet_api.get_group(group_id)
    return group

def assert_repo_with_permission(r1, r2, permission):
    if isinstance(r2, list):
        assert len(r2) == 1
        r2 = r2[0]
    assert r2.id == r1.id
    assert r2.permission == permission
