#include <resea.h>
#include <resea_idl.h>
#include "process.h"
#include "fs.h"

static cid_t memmgr_ch = 1;
static cid_t kernel_ch = 2;
static cid_t server_ch;

error_t handle_fill_page_request(UNUSED cid_t from, pid_t pid, uintptr_t addr,
                                 UNUSED size_t size, page_t *page) {
    struct process *proc = get_process_by_pid(pid);
    assert(proc);

    // TRACE("page fault addr: %p", addr);
    for (int i = 0; i < proc->elf.num_phdrs; i++) {
        struct elf64_phdr *phdr = &proc->elf.phdrs[i];
        if (phdr->p_vaddr <= addr && addr < phdr->p_vaddr + phdr->p_memsz) {
            size_t fileoff = phdr->p_offset + (addr - phdr->p_vaddr);

            page_base_t page_base = valloc(4096);
            uint8_t *data;
            size_t num_pages;
            TRY_OR_PANIC(read_file(proc->file->fs_server, proc->file->fd,
                fileoff, PAGE_SIZE, page_base, (uintptr_t *) &data, &num_pages));

            *page = PAGE_PAYLOAD((uintptr_t) data, 0, PAGE_TYPE_SHARED);
            return OK;
        }
    }

    // zero-filled page
    page_base_t page_base = valloc(4096);
    uint8_t *ptr;
    size_t num_pages;
    TRY_OR_PANIC(alloc_pages(memmgr_ch, 0, page_base, (uintptr_t *) &ptr, &num_pages));
    *page = PAGE_PAYLOAD((uintptr_t) ptr, 0, PAGE_TYPE_SHARED);
    return OK;
}

static error_t handle_printchar(UNUSED cid_t from, uint8_t ch) {
    // Forward to the kernel sever.
    return printchar(kernel_ch, ch);
}

#define FILES_MAX 32
static struct file files[FILES_MAX];
static struct file *open_app_file(const char *path) {
    struct file *f = NULL;
    for (int i = 0; i < FILES_MAX; i++) {
        if (!files[i].using) {
            f = &files[i];
            break;
        }
    }

    assert(f);

    fd_t handle;
    if (open_file(memmgr_ch, path, 0, &handle) != OK) {
        return NULL;
    }

    f->fs_server = memmgr_ch;
    f->fd = handle;
    f->using = true;
    return f;
}

static error_t handle_api_create_app(UNUSED cid_t from, const char* path, pid_t *pid) {
    struct file *f = open_app_file(path);
    if (!f) {
        return ERR_INVALID_MESSAGE;
    }

    TRY_OR_PANIC(create_app(kernel_ch, memmgr_ch, server_ch, f, pid));
    return OK;
}

static error_t handle_api_start_app(UNUSED cid_t from, pid_t pid) {
    TRY_OR_PANIC(start_app(kernel_ch, memmgr_ch, pid));
    return OK;
}

struct join_request {
    bool using;
    cid_t waiter;
    pid_t target;
};

#define JOINS_MAX 16
static struct join_request joins[JOINS_MAX];

static error_t handle_api_join_app(UNUSED cid_t from, pid_t pid, UNUSED int8_t *code) {
    struct join_request *j = NULL;
    for (int i = 0; i < JOINS_MAX; i++) {
        if (!joins[i].using) {
            j = &joins[i];
            break;
        }
    }

    assert(j);

    j->using = true;
    j->target = pid;
    j->waiter = from;
    return ERR_DONT_REPLY;
}

static error_t handle_api_exit_app(UNUSED cid_t from, int8_t code) {
    struct process *proc = get_process_by_cid(from);
    assert(proc);

    struct join_request *j = NULL;
    for (int i = 0; i < JOINS_MAX; i++) {
        if (joins[i].using && joins[i].target == proc->pid) {
            j = &joins[i];
            break;
        }
    }

    assert(j);

    send_api_join_app_reply(j->waiter, code);
    j->using = false;
    return ERR_DONT_REPLY;
}

static void mainloop(void) {
    struct message m;
    TRY_OR_PANIC(ipc_recv(server_ch, &m));
    while (1) {
        struct message r;
        error_t err;
        cid_t from = m.from;
        switch (MSG_TYPE(m.header)) {
        case PRINTCHAR_MSG:
            err = dispatch_printchar(handle_printchar, &m, &r);
            break;
        case FILL_PAGE_REQUEST_MSG:
            err = dispatch_fill_page_request(handle_fill_page_request, &m, &r);
            break;
        case API_CREATE_APP_MSG:
            err = dispatch_api_create_app(handle_api_create_app, &m, &r);
            break;
        case API_START_APP_MSG:
            err = dispatch_api_start_app(handle_api_start_app, &m, &r);
            break;
        case API_JOIN_APP_MSG:
            err = dispatch_api_join_app(handle_api_join_app, &m, &r);
            break;
        case API_EXIT_APP_MSG:
            err = dispatch_api_exit_app(handle_api_exit_app, &m, &r);
            break;
        default:
            WARN("invalid message type %x", MSG_TYPE(m.header));
            err = ERR_INVALID_MESSAGE;
            r.header = ERROR_TO_HEADER(err);
        }

        if (err == ERR_DONT_REPLY) {
            TRY_OR_PANIC(ipc_recv(server_ch, &m));
        } else {
            TRY_OR_PANIC(ipc_replyrecv(from, &r, &m));
        }
    }
}

int main(void) {
    INFO("starting...");
    TRY_OR_PANIC(open(&server_ch));

    cid_t gui_ch;
    TRY_OR_PANIC(connect_server(memmgr_ch, GUI_INTERFACE, &gui_ch));

    process_init();

    for (int i = 0; i < FILES_MAX; i++) {
        files[i].using = false;
    }

    for (int i = 0; i < JOINS_MAX; i++) {
        joins[i].using = false;
    }

    fd_t handle;
    TRY_OR_PANIC(open_file(memmgr_ch, "apps/shell.elf", 0, &handle));

    static struct file shell_file;
    shell_file.fs_server = memmgr_ch;
    shell_file.fd = handle;

    pid_t app_pid;
    TRACE("starting shell app");
    TRY_OR_PANIC(create_app(kernel_ch, memmgr_ch, server_ch, &shell_file, &app_pid));
    TRY_OR_PANIC(start_app(kernel_ch, memmgr_ch, app_pid));
    TRACE("started shell app");

    INFO("entering the mainloop...");
    mainloop();
    return 0;
}
