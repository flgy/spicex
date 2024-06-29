
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <signal.h>
#include <spice-client.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Action {
  ATTACH = 1,
  DETACH = 2,
};

typedef struct _attach_detach_cb_data {
  GSocketConnection *connection;
  SpiceUsbDevice *device;
  guint action;
} attach_detach_cb_data;

typedef struct _incoming_cb_data {
  SpiceSession *session;
  SpiceUsbDeviceManager *mgr;
} incoming_cb_data;

static GMainLoop *mainloop;

static gboolean signal_cb(gpointer);
static void attach_detach_cb(GObject *, GAsyncResult *, gpointer);
static void incoming_cb(GSocketService *, GSocketConnection *, GObject *,
                        incoming_cb_data *);
static void chanel_new_cb(SpiceSession *, SpiceChannel *, gpointer);
static void channel_event_cb(SpiceChannel *, SpiceChannelEvent, SpiceSession *);
void attach_detach_device(GSocketConnection *, SpiceUsbDeviceManager *, guint,
                          guint);
void list_devices(GSocketConnection *, SpiceUsbDeviceManager *);
void _notify_client(GSocketConnection *, gchar *);

void _notify_client(GSocketConnection *connection, gchar *msg) {
  GOutputStream *ostream =
      g_io_stream_get_output_stream(G_IO_STREAM(connection));
  g_output_stream_write(ostream, msg, g_utf8_strlen(msg, 256), NULL, NULL);
}

static gboolean signal_cb(gpointer user_data) {
  g_warning("Received exiting signal, stopping...");
  g_main_loop_quit(mainloop);
  return TRUE;
}

void list_devices(GSocketConnection *connection, SpiceUsbDeviceManager *mgr) {
  int k;
  GError *err = NULL;
  gchar *desc;
  gchar message[256];
  SpiceUsbDevice *device;
  gboolean can_redirect, attached;
  GPtrArray *devices;

  devices = spice_usb_device_manager_get_devices(mgr);
  if (!devices) {
    g_warning("Unable to retrieve devices\n");
    return;
  }
  for (k = 0; k < devices->len; k++) {
    device = g_ptr_array_index(devices, k);
    can_redirect =
        spice_usb_device_manager_can_redirect_device(mgr, device, &err);
    if (err) {
      g_warning("Error when checking redirection status of device %d: %s\n", k,
                err->message);
    }
    desc = spice_usb_device_get_description(device, "%s|%s|%s|%d|%d");
    attached = spice_usb_device_manager_is_device_connected(mgr, device);
    g_snprintf(message, 256, "%d|%s|%d|%d\n", k + 1, desc, can_redirect,
               attached);
    _notify_client(connection, message);
    g_free(desc);
  }
  g_ptr_array_unref(devices);
}

static void attach_detach_cb(GObject *gobject, GAsyncResult *res,
                             gpointer user_data) {
  SpiceUsbDeviceManager *mgr = SPICE_USB_DEVICE_MANAGER(gobject);
  GSocketConnection *connection =
      ((attach_detach_cb_data *)user_data)->connection;
  SpiceUsbDevice *device = ((attach_detach_cb_data *)user_data)->device;
  guint action = ((attach_detach_cb_data *)user_data)->action;
  GError *err = NULL;
  gchar message[8];
  gchar *desc = spice_usb_device_get_description(device, NULL);

  switch (action) {
  case ATTACH:
    spice_usb_device_manager_connect_device_finish(mgr, res, &err);
    if (err) {
      g_prefix_error(&err, "Could not redirect %s: ", desc);
    }
    break;
  case DETACH:
    spice_usb_device_manager_disconnect_device_finish(mgr, res, &err);
    if (err) {
      g_prefix_error(&err, "Could not detach %s: ", desc);
    }
  }

  if (err) {
    g_warning("%s\n", err->message);
    g_error_free(err);
    g_strlcpy(message, "failure", sizeof(message));
  } else {
    g_strlcpy(message, "success", sizeof(message));
  }

  g_free(desc);
  GOutputStream *ostream =
      g_io_stream_get_output_stream(G_IO_STREAM(connection));
  g_output_stream_write(ostream, message, sizeof(message), NULL, NULL);
  g_object_unref(connection);
  g_free(user_data);
}

void attach_detach_device(GSocketConnection *connection,
                          SpiceUsbDeviceManager *mgr, guint action,
                          guint index) {

  GError *err = NULL;
  gboolean failed = FALSE;
  gchar *desc;
  gchar message[256];
  SpiceUsbDevice *device;
  gboolean can_redirect;
  GPtrArray *devices;
  attach_detach_cb_data *user_data;

  devices = spice_usb_device_manager_get_devices(mgr);
  if (!devices) {
    g_warning("Unable to retrieve devices\n");
    _notify_client(connection, "Unable to retrieve devices");
    g_object_unref(connection);
    return;
  }
  if (index > devices->len) {
    g_warning("Device index not found\n");
    _notify_client(connection, "Device index not found");
    g_object_unref(connection);
    g_ptr_array_unref(devices);
    return;
  }

  device = g_ptr_array_index(devices, index - 1);

  user_data = g_new(attach_detach_cb_data, 1);
  user_data->connection = connection;
  user_data->device = device;
  user_data->action = action;

  can_redirect =
      spice_usb_device_manager_can_redirect_device(mgr, device, &err);
  if (err) {
    failed = TRUE;
    g_snprintf(message, sizeof(message),
               "Error when checking redirection status of device %d: %s", index,
               err->message);
  }

  switch (action) {

  case ATTACH:
    if (!can_redirect) {
      failed = TRUE;
      g_snprintf(message, sizeof(message), "Cannot redirect device #%d", index);
    } else if (spice_usb_device_manager_is_device_connected(mgr, device)) {
      failed = TRUE;
      g_snprintf(message, sizeof(message), "Device #%d, already attached",
                 index);
    } else {
      spice_usb_device_manager_connect_device_async(
          mgr, device, NULL, attach_detach_cb, user_data);
    }
    break;

  case DETACH:
    if (!spice_usb_device_manager_is_device_connected(mgr, device)) {
      failed = TRUE;
      g_snprintf(message, sizeof(message), "Device #%d not attached", index);
    } else {
      spice_usb_device_manager_disconnect_device_async(
          mgr, device, NULL, attach_detach_cb, user_data);
    }
  }
  if (failed) {
    g_warning("%s\n", message);
    _notify_client(connection, message);
    g_object_unref(connection);
    g_free(user_data);
  }
  g_free(desc);
  g_ptr_array_unref(devices);
}

static void incoming_cb(GSocketService *service, GSocketConnection *connection,
                        GObject *source_object, incoming_cb_data *user_data) {

  SpiceSession *session = user_data->session;
  SpiceUsbDeviceManager *mgr = user_data->mgr;
  int index = 0;
  gchar *token;

  g_message("Received connection from client");
  GInputStream *istream = g_io_stream_get_input_stream(G_IO_STREAM(connection));

  gchar message[64];
  guint action;
  guint bytes_read =
      g_input_stream_read(istream, message, sizeof(message) - 1, NULL, NULL);
  message[bytes_read] = '\0';
  /*
     Message format is the following
     list:
     attach:index
     detach:index

     index is expected to equal -1 for the `list` action.
  */
  token = strtok(message, ":");
  if (token != NULL) {
    if (g_str_equal(token, "list")) {
      list_devices(connection, mgr);
      return;

    } else {
      if (g_str_equal(token, "attach")) {
        action = ATTACH;
      } else if (g_str_equal(token, "detach")) {
        action = DETACH;
      }
      if (action > 0) {
        token = strtok(NULL, ":");
        if (token != NULL) {
          index = atoi(token);
          if (index > 0) {
            // Dangerous but needed to inform the client
            g_object_ref_sink(connection);
            // connection will be unref by the function callback
            attach_detach_device(connection, mgr, action, index);
            return;
          }
        }
      }
    }
  }
  g_warning("Invalid message format");
  _notify_client(connection, "Invalid message format");
  return;
}

static void channel_new_cb(SpiceSession *session, SpiceChannel *channel,
                           gpointer user_data) {
  int id;
  if (!SPICE_IS_USBREDIR_CHANNEL(channel))
    return;

  g_object_get(channel, "channel-id", &id, NULL);
  if (id != 0)
    return;

  spice_channel_connect(channel);
  g_message("Spice session established");
  g_signal_connect(channel, "channel-event", G_CALLBACK(channel_event_cb),
                   session);
}

static void channel_event_cb(SpiceChannel *channel, SpiceChannelEvent event,
                             SpiceSession *session) {
  switch (event) {
  case SPICE_CHANNEL_OPENED:
    g_message("main channel: opened");
    break;
  case SPICE_CHANNEL_CLOSED:
    g_message("main channel: closed");
    if (session) {
      g_message("Spice session disconnected");
      spice_session_disconnect(session);
    }
    g_main_loop_quit(mainloop);
    break;
  case SPICE_CHANNEL_SWITCHING:
    g_message("main channel: switching host");
    break;
  case SPICE_CHANNEL_ERROR_CONNECT:
    g_message("main channel: failed to connect");
    break;
  default:
    g_warning("unhandled spice main channel event: %u", event);
    break;
  }
}

int main(int argc, char *argv[]) {
  GError *err = NULL;
  gchar *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
  gchar spice_address[] = "spice://127.0.0.1";
  gchar socket_path[256];
  gchar uri[256];
  guint id = 0, spice_port = 0;
  SpiceSession *session = spice_session_new();
  SpiceUsbDeviceManager *mgr = spice_usb_device_manager_get(session, NULL);

  /* Handling arguments */
  if (argc != 2) {
    g_warning("Usage -> %s SPICE_PORT", argv[0]);
    return EXIT_FAILURE;
  }
  spice_port = atoi(argv[1]);
  g_snprintf(uri, sizeof(uri), "%s:%d", spice_address, spice_port);

  mainloop = g_main_loop_new(NULL, false);

  /* Catching UNIX signals */
  GSource *sigint = g_unix_signal_source_new(SIGINT);
  GSource *sigterm = g_unix_signal_source_new(SIGTERM);
  g_source_set_callback(sigint, signal_cb, NULL, NULL);
  g_source_set_callback(sigterm, signal_cb, NULL, NULL);
  g_source_attach(sigint, NULL);
  g_source_attach(sigterm, NULL);

  /* Prepare the spice connection */
  g_object_set(session, "uri", uri, NULL);
  g_signal_connect(session, "channel-new", G_CALLBACK(channel_new_cb), NULL);
  spice_set_session_option(session);

  if (!spice_session_connect(session)) {
    g_warning("Unable to connect to %s uri\n", uri);
    return EXIT_FAILURE;
  }

  /* Prepare the listening socket */
  if (xdg_runtime_dir != NULL) {
    g_snprintf(socket_path, sizeof(socket_path), "%s/%s-%d.sock", xdg_runtime_dir,
               "spicex", spice_port);
  } else {
    g_snprintf(socket_path, sizeof(socket_path), "%s/%s-%d.sock", "/tmp",
               "spicex", spice_port);
  }
  GSocketService *service = g_socket_service_new();
  GSocketAddress *address = g_unix_socket_address_new(socket_path);
  id = g_socket_listener_add_address(
      (GSocketListener *)service, address, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &err);
  if (err != NULL) {
    g_warning("%s: %s\n", err->message, socket_path);
    goto fail;
  }
  incoming_cb_data *user_data = g_new(incoming_cb_data, 1);
  user_data->session = session;
  user_data->mgr = mgr;
  g_signal_connect(service, "incoming", G_CALLBACK(incoming_cb), user_data);
  g_socket_service_start(service);

  /* Running the GTK loop */
  spice_util_set_debug(FALSE);
  g_main_loop_run(mainloop);

  /* Destroying resources */
fail:
  spice_session_disconnect(session);
  g_object_unref(service);
  g_object_unref(address);
  unlink(socket_path);

  if (err != NULL) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
