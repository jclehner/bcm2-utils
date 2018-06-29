#ifndef BCM2DUMP_ASMDEF_H
#define BCM2DUMP_ASMDEF_H

#define BCM2_RWCODE_MAGIC 0xbeefc0de

// pointer to buffer, offset length
#define	BCM2_READ_FUNC_PBOL		0
// buffer, offset, length
#define	BCM2_READ_FUNC_BOL		(1 << 0)
// offset, buffer, length
#define BCM2_READ_FUNC_OBL		(1 << 1)

// offset, length
#define BCM2_ERASE_FUNC_OL		(1 << 8)
// offset, partition size
#define BCM2_ERASE_FUNC_OS		(1 << 9)

// offset, end
#define BCM2_ARGS_OE		(1 << 16)
// offset, length
#define BCM2_ARGS_OL		(1 << 17)

// ignore return value
#define BCM2_RET_VOID		0
// returns zero on success
#define BCM2_RET_OK_0		(1 << 0)
// returns zero on error
#define BCM2_RET_ERR_0		(1 << 1)
// returns length on success
#define BCM2_RET_OK_LEN		(1 << 2)

#endif
