#ifndef EXELINK_CONFIG_H
#define EXELINK_CONFIG_H

#define EXELINK_CONFIG_RESOURCE_ID 101
#define EXELINK_CONFIG_RESOURCE_TYPE RT_RCDATA
#define EXELINK_CONFIG_MAGIC 0x584C5845u /* 'EXLX' little-endian */
#define EXELINK_CONFIG_VERSION 1u

typedef struct exelink_config_header
{
    unsigned int magic;
    unsigned int version;
    unsigned int argc;
    unsigned int envc;
} exelink_config_header;

#endif
