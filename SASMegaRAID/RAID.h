/* Written by Artem Lukyanov <dukzcry@ya.ru> */

#include <sys/systm.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>

#define SPECNAMELEN      63
//#include <dev/mfi/mfi_ioctl.h>

#include "SASMegaRAID.h"

#define MFIIO_PASSTHRU      _IOWR('C', 102, struct mfi_ioc_passthru)
#define MFIIO_QUERY_DISK    _IOWR('Q', 102, struct mfi_query_disk)
#define MFI_MBOX_SIZE       12
//#define MFI_DCMD_FRAME_SIZE 40

/* Scatter Gather elements */
struct mfi_sg32 {
    uint32_t    addr;
    uint32_t    len;
} __packed;

struct mfi_sg64 {
    uint64_t    addr;
    uint32_t    len;
} __packed;

struct mfi_sg_skinny {
    uint64_t    addr;
    uint32_t    len;
    uint32_t    flag;
} __packed;

union mfi_sgl {
    struct mfi_sg32        sg32[1];
    struct mfi_sg64        sg64[1];
    struct mfi_sg_skinny    sg_skinny[1];
} __packed;

struct mfi_frame_header {
    uint8_t        cmd;
    uint8_t        sense_len;
    uint8_t        cmd_status;
    uint8_t        scsi_status;
    uint8_t        target_id;
    uint8_t        lun_id;
    uint8_t        cdb_len;
    uint8_t        sg_count;
    uint32_t    context;
    /*
     * pad0 is MSI Specific. Not used by Driver. Zero the value before
     * sending the command to f/w.
     */
    uint32_t    pad0;
    uint16_t    flags;
#define MFI_FRAME_DATAOUT   0x08
#define MFI_FRAME_DATAIN    0x10
    uint16_t    timeout;
    uint32_t    data_len;
} __packed;

struct mfi_dcmd_frame {
    struct mfi_frame_header header;
    uint32_t    opcode;
    uint8_t        mbox[MFI_MBOX_SIZE];
    union mfi_sgl    sgl;
} __packed;

struct mfi_query_disk {
    uint8_t    array_id;
    uint8_t    present;
    uint8_t    open;
    uint8_t reserved;    /* reserved for future use */
    char    devname[SPECNAMELEN + 1];
} __packed;

struct mfi_ioc_passthru {
    struct mfi_dcmd_frame    ioc_frame;
    uint32_t        buf_size;
    uint8_t            *buf;
} __packed;

class RAID *RAIDP;
class RAID {
    void *raid_devnode;
    int devindex;
    SASMegaRAID *obj;
    
public:
    RAID() {raid_devnode = NULL; devindex = -1;};
    ~RAID();
    
    bool init(SASMegaRAID *, int);
    static int Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
    int MRAID_Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
    int MRAID_UserCommand(struct mfi_ioc_passthru *);
};

static struct cdevsw mraid_cdevsw = {
    (d_open_t *) &nulldev,
    (d_close_t *) &nulldev,
    (d_read_t *) &nulldev,
    (d_write_t *) &nulldev,
    RAID::Ioctl,
    (d_stop_t *) &nulldev,
    (d_reset_t *) &nulldev,
    0,               // struct tty      **d_ttys;
    (d_select_t *) &nulldev,
    eno_mmap,        // mmap_fcn_t       *d_mmap;
    eno_strat,       // strategy_fcn_t   *d_strategy;
    eno_getc,        // getc_fcn_t       *d_getc;
    eno_putc,        // putc_fcn_t       *d_putc;
    D_TTY,           // int               d_type;
};

RAID::~RAID() {
    int ret;
    
    if (raid_devnode != NULL) devfs_remove(raid_devnode);
    if (devindex != -1) {
        ret = cdevsw_remove(devindex, &mraid_cdevsw);
        if (ret != devindex)
            DbgPrint("[RAID] cdevsw_remove() failed (returned %d)\n", ret);
    }
}

bool RAID::init(SASMegaRAID *instance, int domain)
{
    char str[5];
    
    if ((devindex = cdevsw_add(DEVINDEX, &mraid_cdevsw)) == -1) {
        IOPrint("[RAID] cdevsw_add() failed\n");
        return false;
    }
    
    snprintf(str, sizeof(str), "mfi%d", domain);
    
    if ((raid_devnode = devfs_make_node(makedev(devindex, 0),
                                        DEVFS_CHAR,
                                        UID_ROOT,
                                        GID_OPERATOR,
                                        0640,
                                        str)) == NULL)
        return false;
    
    obj = instance;
    
    return true;
}

int RAID::MRAID_UserCommand(struct mfi_ioc_passthru *iop)
{
    mraid_data_mem mem;
    mraid_ccbCommand* ccb;
    IOVirtualAddress addr;
    int res;
    
    if (iop->buf_size > 1024 * 1024 ||
        !(mem.bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
                                                                      kIOMemoryPhysicallyContiguous,
                                                                     iop->buf_size, obj->addr_mask)))
        return ENOMEM;
    mem.bmd->prepare();
    addr = (IOVirtualAddress) mem.bmd->getBytesNoCopy();
    if (iop->buf_size > 0)
        bcopy(iop->buf, (void *) addr, iop->buf_size);
    
    ccb = obj->Getccb();
    res = obj->Do_Management(ccb, iop->ioc_frame.opcode, MRAID_DATA_IN | MRAID_DATA_OUT,
                             iop->buf_size, &mem, iop->ioc_frame.mbox);
    
    obj->Putccb(ccb);
    if (!res) {
        DbgPrint("[RAID] Ioctl failed\n");
        res = EIO;
        goto end;
    }
    else res = 0;
    
    if (iop->buf_size > 0)
        bcopy((void *) addr, iop->buf, iop->buf_size);
    iop->ioc_frame.header.cmd_status = MRAID_STAT_OK;
    
end:
    FreeDataMem(&mem);
    return res;
}

int RAID::MRAID_Ioctl(__unused dev_t dev, u_long cmd, caddr_t data,
                __unused int flag, __unused struct proc *p)
{
    switch (cmd) {
        case MFIIO_QUERY_DISK: {
            struct mfi_query_disk *qd = (struct mfi_query_disk *) data;

            qd->present = obj->sc.sc_ld_present[qd->array_id];
            bzero(qd->devname, SPECNAMELEN + 1);
            snprintf(qd->devname, SPECNAMELEN, "mfid%d", qd->array_id);
            return 0;
        }
        case MFIIO_PASSTHRU: {
            struct mfi_ioc_passthru *iop = (struct mfi_ioc_passthru *) data;
            
            if (obj->EnteredSleep)
                return EIO;
            
            return MRAID_UserCommand(iop);
        }
    }
    
    DbgPrint("[RAID] Ioctl 0x%lx not handled\n", cmd);
    return ENOTTY;
}
int RAID::Ioctl(__unused dev_t dev, u_long cmd, caddr_t data,
                      __unused int flag, __unused struct proc *p)
{
    return RAIDP->MRAID_Ioctl(dev, cmd, data, flag, p);
}
