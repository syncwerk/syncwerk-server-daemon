
from rpcclient import SyncwerkRpcClient as RpcClient
from rpcclient import SyncwerkThreadedRpcClient as ThreadedRpcClient
from rpcclient import MonitorRpcClient as MonitorRpcClient
from rpcclient import SyncwServerRpcClient as ServerRpcClient
from rpcclient import SyncwServerThreadedRpcClient as ServerThreadedRpcClient

class TaskType(object):
    DOWNLOAD = 0
    UPLOAD = 1
