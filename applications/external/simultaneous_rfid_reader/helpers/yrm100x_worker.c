#include "yrm100x_tag.h"
#include "yrm100x_worker.h"

/**
 * File that handles the worker for the YRM100
 * @author frux-c
 * @author modified by haffnerriley
*/

// yrm100 module commands
UHFWorkerEvent verify_module_connected(UHFWorker* uhf_worker) {
    char* hw_version = m100_get_hardware_version(uhf_worker->module);
    char* sw_version = m100_get_software_version(uhf_worker->module);
    char* manufacturer = m100_get_manufacturers(uhf_worker->module);
    // verify all data exists
    if(hw_version == NULL || sw_version == NULL || manufacturer == NULL) return UHFWorkerEventFail;
    return UHFWorkerEventSuccess;
}

UHFTag* send_polling_command(UHFWorker* uhf_worker) {
    // read epc bank
    UHFTag* uhf_tag = uhf_tag_alloc();
    M100ResponseType status;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            uhf_tag_free(uhf_tag);
            return NULL;
        }
        status = m100_single_poll(uhf_worker->module, uhf_tag);
    } while(status != M100SuccessResponse);
    return uhf_tag;
}

//Modified by William Riley Haffner to use a default access password that is set through the ap UI
UHFWorkerEvent read_bank_till_max_length(UHFWorker* uhf_worker, UHFTag* uhf_tag, BankType bank) {
    unsigned int word_low = 0, word_high = 64;
    unsigned int word_size;
    M100ResponseType status;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) return UHFWorkerEventAborted;
        if(word_low >= word_high) return UHFWorkerEventSuccess;
        word_size = (word_low + word_high) / 2;

        status = m100_read_label_data_storage(
            uhf_worker->module, uhf_tag, bank, uhf_worker->DefaultAP, word_size);
        if(status == M100SuccessResponse) {
            word_low = word_size + 1;
        } else if(status == M100MemoryOverrun) {
            word_high = word_size - 1;
        }
    } while(true);
    return UHFWorkerEventSuccess;
}

//Modified by Riley Haffner to read the reserved bank using the default AP set by the user
UHFWorkerEvent read_single_card(UHFWorker* uhf_worker) {
    UHFTag* uhf_tag = send_polling_command(uhf_worker);
    if(uhf_tag == NULL) return UHFWorkerEventAborted;
    uhf_tag_wrapper_set_tag(uhf_worker->uhf_tag_wrapper, uhf_tag);
    // set select
    while(m100_set_select(uhf_worker->module, uhf_tag) != M100SuccessResponse) {
    }
    // read tid

    //create a default password attribute for the worker....
    UHFWorkerEvent event;
    event = read_bank_till_max_length(uhf_worker, uhf_tag, TIDBank);
    if(event != UHFWorkerEventSuccess) return event;
    // read user
    event = read_bank_till_max_length(uhf_worker, uhf_tag, UserBank);
    if(event != UHFWorkerEventSuccess) return event;
    event = read_bank_till_max_length(uhf_worker, uhf_tag, ReservedBank);
    if(event != UHFWorkerEventSuccess) return event;
    return UHFWorkerEventSuccess;
}

//Modified by Riley Haffner to be able to write to the reserved bank
UHFWorkerEvent write_single_card(UHFWorker* uhf_worker) {
    //uhf_worker->TagToWrite
    UHFTag* uhf_tag_des = send_polling_command(uhf_worker);
    if(uhf_tag_des == NULL) return UHFWorkerEventAborted;

    UHFTag* uhf_tag_from = uhf_worker->NewTag;
    M100ResponseType rp_type;
    do {
        rp_type = m100_set_select(uhf_worker->module, uhf_tag_des);
        if(uhf_worker->state == UHFWorkerStateStop) return UHFWorkerEventAborted;
        if(rp_type == M100SuccessResponse) break;
    } while(true);
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_USER)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, UserBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_TID)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, TIDBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_EPC)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, EPCBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_RFU)) {
        if(uhf_worker->KillPwd && uhf_worker->AccessPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                1,
                uhf_worker->DefaultAP);
        } else if(uhf_worker->KillPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                0,
                uhf_worker->DefaultAP);
        } else if(uhf_worker->AccessPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                32,
                uhf_worker->DefaultAP);
        }

        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            break;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAborted;
        }
    }
    return UHFWorkerEventSuccess;
}

int32_t uhf_worker_task(void* ctx) {
    UHFWorker* uhf_worker = ctx;
    if(uhf_worker->state == UHFWorkerStateVerify) {
        UHFWorkerEvent event = verify_module_connected(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateDetectSingle) {
        UHFWorkerEvent event = read_single_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateWriteSingle) {
        UHFWorkerEvent event = write_single_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    }
    return 0;
}

UHFWorker* uhf_worker_alloc() {
    UHFWorker* uhf_worker = (UHFWorker*)malloc(sizeof(UHFWorker));
    uhf_worker->thread =
        furi_thread_alloc_ex("UHFWorker", UHF_WORKER_STACK_SIZE, uhf_worker_task, uhf_worker);
    uhf_worker->module = m100_module_alloc();
    uhf_worker->callback = NULL;
    uhf_worker->ctx = NULL;
    uhf_worker->NewTag = uhf_tag_alloc();
    uhf_worker->KillPwd = false;
    uhf_worker->AccessPwd = false;
    uhf_worker->DefaultAP = 0;
    return uhf_worker;
}

void uhf_worker_change_state(UHFWorker* worker, UHFWorkerState state) {
    worker->state = state;
}

void uhf_worker_start(
    UHFWorker* uhf_worker,
    UHFWorkerState state,
    UHFWorkerCallback callback,
    void* ctx) {
    uhf_worker->state = state;
    uhf_worker->callback = callback;
    uhf_worker->ctx = ctx;
    furi_thread_start(uhf_worker->thread);
}

void uhf_worker_stop(UHFWorker* uhf_worker) {
    furi_assert(uhf_worker);
    furi_assert(uhf_worker->thread);

    if(furi_thread_get_state(uhf_worker->thread) != FuriThreadStateStopped) {
        uhf_worker_change_state(uhf_worker, UHFWorkerStateStop);
        furi_thread_join(uhf_worker->thread);
    }
}

void uhf_worker_free(UHFWorker* uhf_worker) {
    furi_assert(uhf_worker);
    furi_thread_free(uhf_worker->thread);
    m100_module_free(uhf_worker->module);
    uhf_tag_free(uhf_worker->NewTag);
    free(uhf_worker);
}
