#include "process_manager.h"

static TABLE_HASH_FUNC(hash_psn)
{
    unsigned long result = ((ProcessSerialNumber*) key)->lowLongOfPSN;
    result = (result + 0x7ed55d16) + (result << 12);
    result = (result ^ 0xc761c23c) ^ (result >> 19);
    result = (result + 0x165667b1) + (result << 5);
    result = (result + 0xd3a2646c) ^ (result << 9);
    result = (result + 0xfd7046c5) + (result << 3);
    result = (result ^ 0xb55a4f09) ^ (result >> 16);
    return result;
}

static TABLE_COMPARE_FUNC(compare_psn)
{
    return psn_equals(key_a, key_b);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static void
process_manager_add_running_processes(struct process_manager *pm)
{
    ProcessSerialNumber psn = { kNoProcess, kNoProcess };
    while (GetNextProcess(&psn) == noErr) {
        struct process *process = process_create(psn);
        if (!process) continue;

        if (process->lsbackground) {
            debug("%s: %s was marked as background only! ignoring..\n", __FUNCTION__, process->name);
            goto ign;
        }

        if (process->lsuielement) {
            debug("%s: %s was marked as agent! ignoring..\n", __FUNCTION__, process->name);
            goto ign;
        }

        if (process->background) {
            debug("%s: %s was marked as daemon! ignoring..\n", __FUNCTION__, process->name);
            goto ign;
        }

        if (process->xpc) {
            debug("%s: %s was marked as xpc service! ignoring..\n", __FUNCTION__, process->name);
            goto ign;
        }

        if (string_equals(process->name, "Finder")) {
            debug("%s: %s was found! caching psn..\n", __FUNCTION__, process->name);
            pm->finder_psn = psn;
        }

        process_manager_add_process(pm, process);
        goto out;
ign:
        process_destroy(process);
out:;
    }
}
#pragma clang diagnostic pop

struct process *process_manager_find_process(struct process_manager *pm, ProcessSerialNumber *psn)
{
    return table_find(&pm->process, psn);
}

void process_manager_remove_process(struct process_manager *pm, ProcessSerialNumber *psn)
{
    table_remove(&pm->process, psn);
}

void process_manager_add_process(struct process_manager *pm, struct process *process)
{
    table_add(&pm->process, &process->psn, process);
}

#if 0
bool process_manager_next_process(ProcessSerialNumber *next_psn)
{
    CFArrayRef applications =_LSCopyApplicationArrayInFrontToBackOrder(0xFFFFFFFE, 1);
    if (!applications) return false;

    bool found_front_psn = false;
    ProcessSerialNumber front_psn;
    _SLPSGetFrontProcess(&front_psn);

    for (int i = 0; i < CFArrayGetCount(applications); ++i) {
        CFTypeRef asn = CFArrayGetValueAtIndex(applications, i);
        assert(CFGetTypeID(asn) == _LSASNGetTypeID());
        _LSASNExtractHighAndLowParts(asn, &next_psn->highLongOfPSN, &next_psn->lowLongOfPSN);
        if (found_front_psn) break;
        found_front_psn = psn_equals(&front_psn, next_psn);
    }

    CFRelease(applications);
    return true;
}
#endif

void process_manager_init(struct process_manager *pm)
{
    pm->target = GetApplicationEventTarget();
    pm->handler = NewEventHandlerUPP(process_handler);
    pm->type[0].eventClass = kEventClassApplication;
    pm->type[0].eventKind  = kEventAppLaunched;
    pm->type[1].eventClass = kEventClassApplication;
    pm->type[1].eventKind  = kEventAppTerminated;
    pm->type[2].eventClass = kEventClassApplication;
    pm->type[2].eventKind  = kEventAppFrontSwitched;
    table_init(&pm->process, 125, hash_psn, compare_psn);
    process_manager_add_running_processes(pm);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool process_manager_begin(struct process_manager *pm)
{
    ProcessSerialNumber front_psn;
    _SLPSGetFrontProcess(&front_psn);
    GetProcessPID(&front_psn, &g_process_manager.front_pid);
    g_process_manager.last_front_pid = g_process_manager.front_pid;
    return InstallEventHandler(pm->target, pm->handler, 3, pm->type, pm, &pm->ref) == noErr;
}
#pragma clang diagnostic pop

bool process_manager_end(struct process_manager *pm)
{
    return RemoveEventHandler(pm->ref) == noErr;
}
