/*
 * QEMU Arm RME support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright Linaro 2026
 */

#include "qemu/osdep.h"

#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "system/confidential-guest-support.h"
#include "system/kvm.h"
#include "system/runstate.h"

/* ** TEMPORARY LOCATION BEGIN ** */
/*
 * The following should be in linux-headers/asm-arm64/kvm.h
 * when support for Realm guests has landed in mainline Linux.
 */
#if !defined(KVM_ARM_VCPU_REC)
#define KVM_ARM_VCPU_REC        9 /* VCPU REC state as part of Realm */
#endif

#if !defined(KVM_VM_TYPE_ARM_NORMAL)
/*
 * On arm64, machine type can be used to request both the machine type and
 * the physical address size for the VM.
 *
 * Bits[11-8] are reserved for the ARM specific machine type.
 *
 * Bits[7-0] are reserved for the guest PA size shift (i.e, log2(PA_Size)).
 * For backward compatibility, value 0 implies the default IPA size, 40bits.
 */
#define KVM_VM_TYPE_ARM_SHIFT          8
#define KVM_VM_TYPE_ARM_MASK           (0xfULL << KVM_VM_TYPE_ARM_SHIFT)
#define KVM_VM_TYPE_ARM(_type)         \
       (((_type) << KVM_VM_TYPE_ARM_SHIFT) & KVM_VM_TYPE_ARM_MASK)
#define KVM_VM_TYPE_ARM_NORMAL         KVM_VM_TYPE_ARM(0)
#define KVM_VM_TYPE_ARM_REALM          KVM_VM_TYPE_ARM(1)
#endif
/* ** TEMPORARY LOCATION ENDS ** */

#define TYPE_RME_GUEST "rme-guest"
OBJECT_DECLARE_SIMPLE_TYPE(RmeGuest, RME_GUEST)

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(RmeGuest, rme_guest, RME_GUEST,
                                          CONFIDENTIAL_GUEST_SUPPORT,
                                          { TYPE_USER_CREATABLE }, { })

static RmeGuest *rme_guest;

static void rme_vm_state_change(void *opaque, bool running, RunState state)
{
    if (!running) {
        return;
    }

    kvm_mark_guest_state_protected();
}

static void rme_guest_class_init(ObjectClass *oc, const void *data)
{
}

static void rme_guest_init(Object *obj)
{
    if (rme_guest) {
        error_report("a single instance of RmeGuest is supported");
        exit(1);
    }
    rme_guest = RME_GUEST(obj);
}

static void rme_guest_finalize(Object *obj)
{
}

int kvm_arm_rme_init(MachineState *ms)
{
    static Error *rme_mig_blocker;
    ConfidentialGuestSupport *cgs = ms->cgs;

    if (!rme_guest) {
        return 0;
    }

    if (!cgs) {
        error_report("missing -machine confidential-guest-support parameter");
        return -EINVAL;
    }

    error_setg(&rme_mig_blocker, "RME: migration is not implemented");
    migrate_add_blocker(&rme_mig_blocker, &error_fatal);

    /*
     * The realm activation is done last, when the VM starts, after all images
     * have been loaded and all vcpus finalized.
     */
    qemu_add_vm_change_state_handler(rme_vm_state_change, NULL);

    cgs->require_guest_memfd = true;
    cgs->ready = true;
    return 0;
}

void kvm_arm_rme_vcpu_init(ARMCPU *cpu)
{
    if (!rme_guest) {
        return;
    }

    cpu->kvm_rme = true;
    cpu->kvm_init_features[0] |= (1 << KVM_ARM_VCPU_REC);
}

int kvm_arm_rme_vm_type(void)
{
    if (rme_guest) {
        return KVM_VM_TYPE_ARM_REALM;
    }
    return 0;
}
