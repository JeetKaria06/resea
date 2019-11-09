#ifndef __IPC_H__
#define __IPC_H__

#include <types.h>
#include <message.h>

/// Determines if the specifed system call satisfies fastpath prerequisites:
///
///   - The system call is `ipc`.
///   - Both IPC_SEND and IPC_RECV are set.
///
/// Note that we ignore IPC_NOBLOCK as the fastpath handles both blocking and
/// non-blocking IPC properly: it falls back into sys_ipc() if the IPC operation
/// would block.
///
#define FASTPATH_SYSCALL_TEST(syscall) \
    ((syscall) & ~(IPC_NOBLOCK)) == (SYSCALL_IPC | IPC_SEND | IPC_RECV)

/// Determines if a message satisfies a fastpath prerequisite. This test checks
/// that page/channel payloads are not contained in the message.
#define FASTPATH_HEADER_TEST(header) (((header) & 0x1800ULL) == 0)

/// Checks whether the message is worth tracing.
static inline bool is_annoying_msg(header_t msg_type) {
    return msg_type == RUNTIME_PRINTCHAR_MSG
           || msg_type == RUNTIME_PRINTCHAR_REPLY_MSG
           || msg_type == RUNTIME_PRINT_STR_MSG
           || msg_type == RUNTIME_PRINT_STR_REPLY_MSG
           || msg_type == PAGER_FILL_MSG
           || msg_type == PAGER_FILL_REPLY_MSG
           || msg_type == IO_READ_IOPORT_MSG
           || msg_type == IO_READ_IOPORT_REPLY_MSG
           || msg_type == IO_WRITE_IOPORT_MSG
           || msg_type == IO_WRITE_IOPORT_REPLY_MSG;
}

struct packet_header {
    char src_proc_name[32];
    char dst_proc_name[32];
    cid_t src_cid;
    cid_t dst_cid;
} PACKED;
STATIC_ASSERT(sizeof(uint32_t) == sizeof(cid_t));

/// Dumps the message for debugging with Wireshark.
static inline void dump_message(struct channel *src, struct channel *dst, struct message *m) {
    struct packet_header header;
    strcpy(header.dst_proc_name, 32, dst->process->name);
    header.dst_cid = dst->cid;
    strcpy(header.src_proc_name, 32, src->process->name);
    header.src_cid = src->cid;

    size_t len =
        sizeof(header) + MSG_COMMON_HEADER_LEN + INLINE_PAYLOAD_LEN(m->header);
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printk("%s[kernel] pcap> %04x ", (i == 0) ? "" : "\n", i);
        }

        uint8_t byte;
        if (i >= sizeof(header)) {
            byte = ((uint8_t *) m)[i - sizeof(header)];
        } else {
            byte = ((uint8_t *) &header)[i];
        }

        printk("%02x ", byte);
    }

    printk("\n");
}

#define IPC_TRACE(m, fmt, ...)                        \
    do {                                              \
        if (!is_annoying_msg(m->header)) {            \
            TRACE(fmt, ## __VA_ARGS__);               \
        }                                             \
    } while (0)

#define DUMP_MESSAGE(src, dst, m)                     \
    do {                                              \
        if (!is_annoying_msg(m->header)) {            \
            dump_message(src, dst, m);                \
        }                                             \
    } while (0)

cid_t sys_open(void);
error_t sys_close(cid_t cid);
error_t kernel_ipc(cid_t cid, uint32_t syscall);
error_t sys_ipc(cid_t cid, uint32_t syscall);
error_t sys_notify(cid_t cid, notification_t notification);
int syscall_handler(uintmax_t arg0, uintmax_t arg1, uintmax_t syscall);

#endif
