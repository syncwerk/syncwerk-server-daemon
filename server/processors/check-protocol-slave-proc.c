/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include "check-protocol-slave-proc.h"

G_DEFINE_TYPE (SyncwerkCheckProtocolSlaveProc, syncwerk_check_protocol_slave_proc, CCNET_TYPE_PROCESSOR)

static int
check_protocol_slave_start (CcnetProcessor *processor, int argc, char **argv);

static void
syncwerk_check_protocol_slave_proc_class_init (SyncwerkCheckProtocolSlaveProcClass *klass)
{
    CcnetProcessorClass *proc_class = CCNET_PROCESSOR_CLASS (klass);

    proc_class->name = "syncwerk-check-protocol-slave-proc";
    proc_class->start = check_protocol_slave_start;
}

static void
syncwerk_check_protocol_slave_proc_init (SyncwerkCheckProtocolSlaveProc *processor)
{
}


static int
check_protocol_slave_start (CcnetProcessor *processor, int argc, char **argv)
{
    int n;
    char buf[10];
    n = snprintf (buf, sizeof(buf), "%d", CURRENT_PROTO_VERSION);
    ccnet_processor_send_response (processor, SC_OK, SS_OK, buf, n+1);
    ccnet_processor_done (processor, TRUE);

    return 0;
}
