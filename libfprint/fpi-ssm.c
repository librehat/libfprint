/*
 * Functions to assist with asynchronous driver <---> library communications
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 * Copyright (C) 2019 Marco Trevisan <marco.trevisan@canonical.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "SSM"

#include "drivers_api.h"
#include "fpi-ssm.h"


/**
 * SECTION:fpi-ssm
 * @title: Sequential state machine
 * @short_description: State machine helpers
 *
 * Asynchronous driver design encourages some kind of state machine behind it.
 * In most cases, the state machine is entirely linear - you only go to the
 * next state, you never jump or go backwards. The #FpiSsm functions help you
 * implement such a machine.
 *
 * e.g. `S1` ↦ `S2` ↦ `S3` ↦ `S4`
 *
 * `S1` is the start state
 * There is also an implicit error state and an implicit accepting state
 * (both with implicit edges from every state).
 *
 * You can also jump to any arbitrary state (while marking completion of the
 * current state) while the machine is running. In other words there are
 * implicit edges linking one state to every other state.
 *
 * To create an #fpi_ssm, you pass a state handler function and the total number of
 * states (4 in the above example) to fpi_ssm_new (). Note that the state numbers
 * start at zero, making them match the first value in a C enumeration.
 *
 * To start a ssm, you pass in a completion callback function to fpi_ssm_start()
 * which gets called when the ssm completes (both on error and on failure).
 * Starting a ssm also takes ownership of it.
 *
 * To iterate to the next state, call fpi_ssm_next_state(). It is legal to
 * attempt to iterate beyond the final state - this is equivalent to marking
 * the ssm as successfully completed.
 *
 * To mark successful completion of a SSM, either iterate beyond the final
 * state or call fpi_ssm_mark_completed() from any state.
 * This will also invalidate the machine, freeing it.
 *
 * To mark failed completion of a SSM, call fpi_ssm_mark_failed() from any
 * state. You must pass a non-zero error code.
 *
 * Your state handling function looks at the return value of
 * fpi_ssm_get_cur_state() in order to determine the current state and hence
 * which operations to perform (a switch statement is appropriate).
 *
 * Typically, the state handling function fires off an asynchronous
 * communication with the device (such as a libsub transfer), and the
 * callback function iterates the machine to the next state
 * upon success (or fails).
 *
 * Your completion callback should examine the return value of
 * fpi_ssm_get_error() in ordater to determine whether the #FpiSsm completed or
 * failed. An error code of zero indicates successful completion.
 */

struct _FpiSsm
{
  FpDevice               *dev;
  FpiSsm                 *parentsm;
  gpointer                ssm_data;
  GDestroyNotify          ssm_data_destroy;
  int                     nr_states;
  int                     cur_state;
  gboolean                completed;
  GSource                *timeout;
  GError                 *error;
  FpiSsmCompletedCallback callback;
  FpiSsmHandlerCallback   handler;
};

/**
 * fpi_ssm_new:
 * @dev: a #fp_dev fingerprint device
 * @handler: the callback function
 * @nr_states: the number of states
 *
 * Allocate a new ssm, with @nr_states states. The @handler callback
 * will be called after each state transition.
 *
 * Returns: a new #FpiSsm state machine
 */
FpiSsm *
fpi_ssm_new (FpDevice             *dev,
             FpiSsmHandlerCallback handler,
             int                   nr_states)
{
  FpiSsm *machine;

  BUG_ON (nr_states < 1);

  machine = g_new0 (FpiSsm, 1);
  machine->handler = handler;
  machine->nr_states = nr_states;
  machine->dev = dev;
  machine->completed = TRUE;
  return machine;
}

/**
 * fpi_ssm_set_data:
 * @machine: an #FpiSsm state machine
 * @ssm_data: (nullable): a pointer to machine data
 * @ssm_data_destroy: (nullable): #GDestroyNotify for @ssm_data
 *
 * Sets @machine's data (freeing the existing data, if any).
 */
void
fpi_ssm_set_data (FpiSsm        *machine,
                  gpointer       ssm_data,
                  GDestroyNotify ssm_data_destroy)
{
  if (machine->ssm_data_destroy && machine->ssm_data)
    machine->ssm_data_destroy (machine->ssm_data);

  machine->ssm_data = ssm_data;
  machine->ssm_data_destroy = ssm_data_destroy;
}

/**
 * fpi_ssm_get_data:
 * @machine: an #FpiSsm state machine
 *
 * Retrieve the pointer to SSM data set with fpi_ssm_set_ssm_data()
 *
 * Returns: a pointer
 */
void *
fpi_ssm_get_data (FpiSsm *machine)
{
  return machine->ssm_data;
}

/**
 * fpi_ssm_free:
 * @machine: an #FpiSsm state machine
 *
 * Frees a state machine. This does not call any error or success
 * callbacks, so you need to do this yourself.
 */
void
fpi_ssm_free (FpiSsm *machine)
{
  if (!machine)
    return;

  if (machine->ssm_data_destroy)
    g_clear_pointer (&machine->ssm_data, machine->ssm_data_destroy);
  g_clear_pointer (&machine->error, g_error_free);
  g_clear_pointer (&machine->timeout, g_source_destroy);
  g_free (machine);
}

/* Invoke the state handler */
static void
__ssm_call_handler (FpiSsm *machine)
{
  fp_dbg ("%p entering state %d", machine, machine->cur_state);
  machine->handler (machine, machine->dev);
}

/**
 * fpi_ssm_start:
 * @ssm: (transfer full): an #FpiSsm state machine
 * @callback: the #FpiSsmCompletedCallback callback to call on completion
 *
 * Starts a state machine. You can also use this function to restart
 * a completed or failed state machine. The @callback will be called
 * on completion.
 *
 * Note that @ssm will be stolen when this function is called.
 * So that all associated data will be free'ed automatically, after the
 * @callback is ran.
 */
void
fpi_ssm_start (FpiSsm *ssm, FpiSsmCompletedCallback callback)
{
  BUG_ON (!ssm->completed);
  ssm->callback = callback;
  ssm->cur_state = 0;
  ssm->completed = FALSE;
  ssm->error = NULL;
  __ssm_call_handler (ssm);
}

static void
__subsm_complete (FpiSsm *ssm, FpDevice *_dev, GError *error)
{
  FpiSsm *parent = ssm->parentsm;

  BUG_ON (!parent);
  if (error)
    fpi_ssm_mark_failed (parent, error);
  else
    fpi_ssm_next_state (parent);
}

/**
 * fpi_ssm_start_subsm:
 * @parent: an #FpiSsm state machine
 * @child: an #FpiSsm state machine
 *
 * Starts a state machine as a child of another. if the child completes
 * successfully, the parent will be advanced to the next state. if the
 * child fails, the parent will be marked as failed with the same error code.
 *
 * The child will be automatically freed upon completion or failure.
 */
void
fpi_ssm_start_subsm (FpiSsm *parent, FpiSsm *child)
{
  BUG_ON (parent->timeout);
  child->parentsm = parent;
  g_clear_pointer (&parent->timeout, g_source_destroy);
  fpi_ssm_start (child, __subsm_complete);
}

/**
 * fpi_ssm_mark_completed:
 * @machine: an #FpiSsm state machine
 *
 * Mark a ssm as completed successfully. The callback set when creating
 * the state machine with fpi_ssm_new () will be called synchronously.
 */
void
fpi_ssm_mark_completed (FpiSsm *machine)
{
  BUG_ON (machine->completed);
  BUG_ON (machine->timeout);
  BUG_ON (machine->timeout != NULL);

  g_clear_pointer (&machine->timeout, g_source_destroy);
  machine->completed = TRUE;

  if (machine->error)
    fp_dbg ("%p completed with error: %s", machine, machine->error->message);
  else
    fp_dbg ("%p completed successfully", machine);
  if (machine->callback)
    {
      GError *error = machine->error ? g_error_copy (machine->error) : NULL;

      machine->callback (machine, machine->dev, error);
    }
  fpi_ssm_free (machine);
}

/**
 * fpi_ssm_mark_failed:
 * @machine: an #FpiSsm state machine
 * @error: a #GError
 *
 * Mark a state machine as failed with @error as the error code, completing it.
 */
void
fpi_ssm_mark_failed (FpiSsm *machine, GError *error)
{
  g_assert (error);
  if (machine->error)
    {
      fp_warn ("SSM already has an error set, ignoring new error %s", error->message);
      g_error_free (error);
      return;
    }

  fp_dbg ("SSM failed in state %d with error: %s", machine->cur_state, error->message);
  machine->error = error;
  fpi_ssm_mark_completed (machine);
}

/**
 * fpi_ssm_next_state:
 * @machine: an #FpiSsm state machine
 *
 * Iterate to next state of a state machine. If the current state is the
 * last state, then the state machine will be marked as completed, as
 * if calling fpi_ssm_mark_completed().
 */
void
fpi_ssm_next_state (FpiSsm *machine)
{
  g_return_if_fail (machine != NULL);

  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  g_clear_pointer (&machine->timeout, g_source_destroy);

  machine->cur_state++;
  if (machine->cur_state == machine->nr_states)
    fpi_ssm_mark_completed (machine);
  else
    __ssm_call_handler (machine);
}

void
fpi_ssm_cancel_delayed_state_change (FpiSsm *machine)
{
  g_return_if_fail (machine);
  BUG_ON (machine->completed);
  BUG_ON (machine->timeout == NULL);

  g_clear_pointer (&machine->timeout, g_source_destroy);
}

static void
on_device_timeout_next_state (FpDevice *dev,
                              gpointer  user_data)
{
  FpiSsm *machine = user_data;

  machine->timeout = NULL;
  fpi_ssm_next_state (machine);
}

/**
 * fpi_ssm_next_state_delayed:
 * @machine: an #FpiSsm state machine
 * @delay: the milliseconds to wait before switching to the next state
 *
 * Iterate to next state of a state machine with a delay of @delay ms. If the
 * current state is the last state, then the state machine will be marked as
 * completed, as if calling fpi_ssm_mark_completed().
 */
void
fpi_ssm_next_state_delayed (FpiSsm *machine,
                            int     delay)
{
  g_autofree char *source_name = NULL;

  g_return_if_fail (machine != NULL);
  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  g_clear_pointer (&machine->timeout, g_source_destroy);
  machine->timeout = fpi_device_add_timeout (machine->dev, delay,
                                             on_device_timeout_next_state,
                                             machine);

  source_name = g_strdup_printf ("[%s] ssm %p jump to next state %d",
                                 fp_device_get_device_id (machine->dev),
                                 machine, machine->cur_state + 1);
  g_source_set_name (machine->timeout, source_name);
}

/**
 * fpi_ssm_jump_to_state:
 * @machine: an #FpiSsm state machine
 * @state: the state to jump to
 *
 * Jump to the @state state, bypassing intermediary states.
 * If @state is the last state, the machine won't be completed unless
 * fpi_ssm_mark_completed() isn't explicitly called.
 */
void
fpi_ssm_jump_to_state (FpiSsm *machine, int state)
{
  BUG_ON (machine->completed);
  BUG_ON (state < 0 || state >= machine->nr_states);
  BUG_ON (machine->timeout != NULL);

  g_clear_pointer (&machine->timeout, g_source_destroy);
  machine->cur_state = state;
  __ssm_call_handler (machine);
}

typedef struct
{
  FpiSsm *machine;
  int     next_state;
} FpiSsmJumpToStateDelayedData;

static void
on_device_timeout_jump_to_state (FpDevice *dev,
                                 gpointer  user_data)
{
  FpiSsmJumpToStateDelayedData *data = user_data;

  data->machine->timeout = NULL;
  fpi_ssm_jump_to_state (data->machine, data->next_state);
}

/**
 * fpi_ssm_jump_to_state_delayed:
 * @machine: an #FpiSsm state machine
 * @state: the state to jump to
 * @delay: the milliseconds to wait before switching to @state state
 *
 * Jump to the @state state with a delay of @delay milliseconds, bypassing
 * intermediary states.
 */
void
fpi_ssm_jump_to_state_delayed (FpiSsm *machine,
                               int     state,
                               int     delay)
{
  FpiSsmJumpToStateDelayedData *data;
  g_autofree char *source_name = NULL;

  g_return_if_fail (machine != NULL);
  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  data = g_new0 (FpiSsmJumpToStateDelayedData, 1);
  data->machine = machine;
  data->next_state = state;

  g_clear_pointer (&machine->timeout, g_source_destroy);
  machine->timeout = fpi_device_add_timeout_full (machine->dev, delay,
                                                  on_device_timeout_jump_to_state,
                                                  data, g_free);

  source_name = g_strdup_printf ("[%s] ssm %p jump to state %d",
                                 fp_device_get_device_id (machine->dev),
                                 machine, state);
  g_source_set_name (machine->timeout, source_name);
}

/**
 * fpi_ssm_get_cur_state:
 * @machine: an #FpiSsm state machine
 *
 * Returns the value of the current state. Note that states are
 * 0-indexed, so a value of 0 means “the first state”.
 *
 * Returns: the current state.
 */
int
fpi_ssm_get_cur_state (FpiSsm *machine)
{
  return machine->cur_state;
}

/**
 * fpi_ssm_get_error:
 * @machine: an #FpiSsm state machine
 *
 * Returns the error code set by fpi_ssm_mark_failed().
 *
 * Returns: (transfer none): a error code
 */
GError *
fpi_ssm_get_error (FpiSsm *machine)
{
  return machine->error;
}

/**
 * fpi_ssm_dup_error:
 * @machine: an #FpiSsm state machine
 *
 * Returns the error code set by fpi_ssm_mark_failed().
 *
 * Returns: (transfer full): a error code
 */
GError *
fpi_ssm_dup_error (FpiSsm *machine)
{
  if (machine->error)
    return g_error_copy (machine->error);

  return NULL;
}

/**
 * fpi_ssm_usb_transfer_cb:
 * @transfer: a #FpiUsbTransfer
 * @device: a #FpDevice
 * @unused_data: User data (unused)
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiUsbTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_usb_transfer_cb (FpiUsbTransfer *transfer, FpDevice *device,
                         gpointer unused_data, GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (error)
    fpi_ssm_mark_failed (transfer->ssm, error);
  else
    fpi_ssm_next_state (transfer->ssm);
}

/**
 * fpi_ssm_usb_transfer_with_weak_pointer_cb:
 * @transfer: a #FpiUsbTransfer
 * @device: a #FpDevice
 * @weak_ptr: A #gpointer pointer to nullify. You can pass a pointer to any
 *            #gpointer to nullify when the callback is completed. I.e a
 *            pointer to the current #FpiUsbTransfer.
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiUsbTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 * Passing a #gpointer* as @weak_ptr permits to nullify it once we're done
 * with the transfer.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_usb_transfer_with_weak_pointer_cb (FpiUsbTransfer *transfer,
                                           FpDevice *device, gpointer weak_ptr,
                                           GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (weak_ptr)
    g_nullify_pointer ((gpointer *) weak_ptr);

  fpi_ssm_usb_transfer_cb (transfer, device, weak_ptr, error);
}
