#include <barrelfish/barrelfish.h>
#include <barrelfish/monitor_client.h>
#include <if/monitor_defs.h>
#include <barrelfish/dispatcher_arch.h>
#include <bench/bench.h>
#include <barrelfish/spawn_client.h>
#include <barrelfish/nameservice_client.h>

const int target_core = 2;

static void attach_reply(struct monitor_binding *b, coreid_t succ_core) {
    printf("Coreid %hhu attached\n", succ_core);
}

static void attach_test(void) {
    struct monitor_binding *mb = get_monitor_binding();
 
    mb->rx_vtbl.attach_group_reply = attach_reply;

    monitor_attach_group_request__tx(mb, NOP_CONT, target_core, get_core_id());
    while (1) {
        messages_wait_and_handle_next();
    }
}

static int test_thread(void* args) {
    while (1) {
        printf("thread %d %d\n", get_core_id(), (int)args);
        for (int i = 0; i < 9000000 * 5; i++);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct thread *a = thread_create(test_thread, (void *)10);
    struct thread *b = thread_create(test_thread, (void *)20);

    dispatcher_handle_t handle = curdispatcher();
    printf("handle is %x, core id: %d\n", handle, get_core_id());
    struct thread *me = CURRENT_THREAD;
    if (me)
    {
        printf("origin thread affinity %llx\n", thread_get_affinity(me));
    }
    else
    {
        printf("CURRENT_THREAD is empty\n");
    }
    thread_set_affinity(a, 1 << target_core);
    printf("thread affinity set\n");
    attach_test();

    thread_join(a, NULL);
    thread_join(b, NULL);
    return 0;
}