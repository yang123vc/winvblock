/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
 *
 * This file is part of WinVBlock, derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Device specifics.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "debug.h"

/* Forward declarations. */
static device__free_func device__free_dev_;
static device__create_pdo_func device__make_pdo_;

/**
 * Initialize device defaults.
 *
 * @v dev               Points to the device to initialize with defaults.
 */
winvblock__lib_func void device__init(struct device__type * dev) {
    RtlZeroMemory(dev, sizeof dev);
    /* Populate non-zero device defaults. */
    dev->DriverObject = driver__obj_ptr;
    dev->ops.create_pdo = device__make_pdo_;
    dev->ops.free = device__free_dev_;
  }

/**
 * Create a new device.
 *
 * @ret dev             The address of a new device, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * device__type and populate the device with default values.
 */
winvblock__lib_func struct device__type * device__create(void) {
    struct device__type * dev;

    /*
     * Devices might be used for booting and should
     * not be allocated from a paged memory pool.
     */
    dev = wv_malloc(sizeof *dev);
    if (dev == NULL)
      return NULL;

    device__init(dev);
    return dev;
  }

/**
 * Create a device PDO.
 *
 * @v dev               Points to the device that needs a PDO.
 */
winvblock__lib_func PDEVICE_OBJECT STDCALL device__create_pdo(
    IN struct device__type * dev
  ) {
    return dev->ops.create_pdo(dev);
  }

/**
 * Default PDO creation operation.
 *
 * @v dev               Points to the device that needs a PDO.
 * @ret NULL            Reports failure, no matter what.
 *
 * This function does nothing, since it doesn't make sense to create a PDO
 * for an unknown type of device.
 */
static PDEVICE_OBJECT STDCALL device__make_pdo_(IN struct device__type * dev) {
    DBG("No specific PDO creation operation for this device!\n");
    return NULL;
  }

/**
 * Respond to a device PnP ID query.
 *
 * @v dev                       The device being queried for PnP IDs.
 * @v query_type                The query type.
 * @v buf                       Wide character, 512-element buffer for the
 *                              ID response.
 * @ret winvblock__uint32       The number of wide characters in the response,
 *                              or 0 upon a failure.
 */
winvblock__uint32 STDCALL device__pnp_id(
    IN struct device__type * dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    return dev->ops.pnp_id ? dev->ops.pnp_id(dev, query_type, buf) : 0;
  }

/* An IRP handler for a PnP ID query. */
NTSTATUS STDCALL device__pnp_query_id(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    NTSTATUS status;
    WCHAR (*str)[512];
    winvblock__uint32 str_len;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    /* Allocate the working buffer. */
    str = wv_mallocz(sizeof *str);
    if (str == NULL) {
        DBG("wv_malloc IRP_MN_QUERY_ID\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_str;
      }
    /* Invoke the specific device's ID query. */
    str_len = device__pnp_id(
        dev,
        io_stack_loc->Parameters.QueryId.IdType,
        str
      );
    if (str_len == 0) {
        irp->IoStatus.Information = 0;
        status = STATUS_NOT_SUPPORTED;
        goto alloc_info;
      }
    /* Allocate the return buffer. */
    irp->IoStatus.Information = (ULONG_PTR) wv_palloc(str_len * sizeof **str);
    if (irp->IoStatus.Information == 0) {
        DBG("wv_palloc failed.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_info;
      }
    /* Copy the working buffer to the return buffer. */
    RtlCopyMemory(
        (void *) irp->IoStatus.Information,
        str,
        str_len * sizeof **str
      );
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information not freed. */
    alloc_info:

    wv_free(str);
    alloc_str:

    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

/**
 * Close a device.
 *
 * @v dev               Points to the device to close.
 */
winvblock__lib_func void STDCALL device__close(
    IN struct device__type * dev
  ) {
    /* Call the device's close routine. */
    dev->ops.close(dev);
    return;
  }

/**
 * Delete a device.
 *
 * @v dev               Points to the device to delete.
 */
winvblock__lib_func void STDCALL device__free(IN struct device__type * dev) {
    /* Call the device's free routine. */
    dev->ops.free(dev);
  }

/**
 * Default device deletion operation.
 *
 * @v dev               Points to the device to delete.
 */
static void STDCALL device__free_dev_(IN struct device__type * dev) {
    wv_free(dev);
  }

/**
 * Get a device from a DEVICE_OBJECT.
 *
 * @v dev_obj           Points to the DEVICE_OBJECT to get the device from.
 * @ret                 Returns a pointer to the device on success, else NULL.
 */
winvblock__lib_func struct device__type * device__get(PDEVICE_OBJECT dev_obj) {
    driver__dev_ext_ptr dev_ext;

    if (!dev_obj)
      return NULL;
    dev_ext = dev_obj->DeviceExtension;
    return dev_ext->device;
  }

/**
 * Set the device for a DEVICE_OBJECT.
 *
 * @v dev_obj           Points to the DEVICE_OBJECT to set the device for.
 * @v dev               Points to the device to associate with.
 */
winvblock__lib_func void device__set(
    PDEVICE_OBJECT dev_obj,
    struct device__type * dev
  ) {
    driver__dev_ext_ptr dev_ext = dev_obj->DeviceExtension;
    dev_ext->device = dev;
    return;
  }
