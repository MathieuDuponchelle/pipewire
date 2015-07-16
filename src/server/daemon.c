/* Pinos
 * Copyright (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gio/gio.h>

#include "config.h"

#include "client/pinos.h"

#include "server/daemon.h"
#include "server/client.h"

#include "dbus/org-pinos.h"

#define PINOS_DAEMON_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), PINOS_TYPE_DAEMON, PinosDaemonPrivate))

struct _PinosDaemonPrivate
{
  guint id;
  GDBusConnection *connection;
  GDBusObjectManagerServer *server_manager;

  GList *sources;

  GHashTable *senders;
};

typedef struct {
  guint id;
  gchar *sender;
  PinosDaemon *daemon;
  GList *objects;
} SenderData;

static void
client_name_appeared_handler (GDBusConnection *connection,
                              const gchar     *name,
                              const gchar     *name_owner,
                              gpointer         user_data)
{
  SenderData *data = user_data;
  PinosDaemonPrivate *priv = data->daemon->priv;

  g_hash_table_insert (priv->senders, data->sender, data);

  if (!g_strcmp0 (name, g_dbus_connection_get_unique_name (connection)))
    return;
}

static void
client_name_vanished_handler (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
  SenderData *data = user_data;

  g_bus_unwatch_name (data->id);
}

static void
data_free (SenderData *data)
{
  g_list_free_full (data->objects, g_object_unref);
  g_hash_table_remove (data->daemon->priv->senders, data->sender);
  g_free (data->sender);
  g_free (data);
}

static SenderData *
sender_data_new (PinosDaemon *daemon,
                 const gchar *sender)
{
  PinosDaemonPrivate *priv = daemon->priv;
  SenderData *data;

  data = g_new0 (SenderData, 1);
  data->daemon = daemon;
  data->sender = g_strdup (sender);

  data->id = g_bus_watch_name_on_connection (priv->connection,
                                    sender,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    client_name_appeared_handler,
                                    client_name_vanished_handler,
                                    data,
                                    (GDestroyNotify) data_free);


  return data;
}

static void
handle_disconnect_client (PinosClient *client,
                          gpointer     user_data)
{
  PinosDaemon *daemon = user_data;
  PinosDaemonPrivate *priv = daemon->priv;
  const gchar *sender;
  SenderData *data;

  sender = pinos_client_get_sender (client);

  data = g_hash_table_lookup (priv->senders, sender);
  if (data == NULL)
    return;

  data->objects = g_list_remove (data->objects, client);
  g_object_unref (client);
}

static gboolean
handle_connect_client (PinosDaemon1           *interface,
                       GDBusMethodInvocation  *invocation,
                       GVariant               *arg_properties,
                       gpointer                user_data)
{
  PinosDaemon *daemon = user_data;
  PinosDaemonPrivate *priv = daemon->priv;
  PinosClient *client;
  const gchar *sender, *object_path;
  SenderData *data;

  sender = g_dbus_method_invocation_get_sender (invocation);

  client = pinos_client_new (daemon, sender, PINOS_DBUS_OBJECT_PREFIX, arg_properties);
  g_signal_connect (client, "disconnect", (GCallback) handle_disconnect_client, daemon);

  data = g_hash_table_lookup (priv->senders, sender);
  if (data == NULL)
    data = sender_data_new (daemon, sender);

  data->objects = g_list_prepend (data->objects, client);

  object_path = pinos_client_get_object_path (client);
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(o)", object_path));

  return TRUE;
}

static void
export_server_object (PinosDaemon              *daemon,
                      GDBusObjectManagerServer *manager)
{
  PinosObjectSkeleton *skel;

  skel = pinos_object_skeleton_new (PINOS_DBUS_OBJECT_SERVER);
  {
    PinosDaemon1 *iface;

    iface = pinos_daemon1_skeleton_new ();
    g_signal_connect (iface, "handle-connect-client", (GCallback) handle_connect_client, daemon);
    pinos_daemon1_set_user_name (iface, g_get_user_name ());
    pinos_daemon1_set_host_name (iface, g_get_host_name ());
    pinos_daemon1_set_version (iface, PACKAGE_VERSION);
    pinos_daemon1_set_name (iface, PACKAGE_NAME);
    pinos_object_skeleton_set_daemon1 (skel, iface);
    g_object_unref (iface);
  }
  g_dbus_object_manager_server_export (manager, G_DBUS_OBJECT_SKELETON (skel));
  g_object_unref (skel);
}

static void
bus_acquired_handler (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  PinosDaemon *daemon = user_data;
  PinosDaemonPrivate *priv = daemon->priv;
  GDBusObjectManagerServer *manager = priv->server_manager;

  priv->connection = connection;

  export_server_object (daemon, manager);

  g_dbus_object_manager_server_set_connection (manager, connection);
}

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  PinosDaemon *daemon = user_data;
  PinosDaemonPrivate *priv = daemon->priv;
  GDBusObjectManagerServer *manager = priv->server_manager;

  g_dbus_object_manager_server_unexport (manager, PINOS_DBUS_OBJECT_SERVER);
  g_dbus_object_manager_server_set_connection (manager, connection);
  priv->connection = connection;
}

/**
 * pinos_daemon_new:
 *
 * Make a new #PinosDaemon object
 *
 * Returns: a new #PinosDaemon
 */
PinosDaemon *
pinos_daemon_new (void)
{
  return g_object_new (PINOS_TYPE_DAEMON, NULL);
}

/**
 * pinos_daemon_start:
 * @daemon: a #PinosDaemon
 *
 * Start the @daemon.
 */
void
pinos_daemon_start (PinosDaemon *daemon)
{
  PinosDaemonPrivate *priv;

  g_return_if_fail (PINOS_IS_DAEMON (daemon));

  priv = daemon->priv;
  g_return_if_fail (priv->id == 0);

  priv->id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             PINOS_DBUS_SERVICE,
                             G_BUS_NAME_OWNER_FLAGS_REPLACE,
                             bus_acquired_handler,
                             name_acquired_handler,
                             name_lost_handler,
                             daemon,
                             NULL);
}

/**
 * pinos_daemon_stop:
 * @daemon: a #PinosDaemon
 *
 * Stop the @daemon.
 */
void
pinos_daemon_stop (PinosDaemon *daemon)
{
  PinosDaemonPrivate *priv = daemon->priv;

  g_return_if_fail (PINOS_IS_DAEMON (daemon));

  if (priv->id != 0) {
    g_bus_unown_name (priv->id);
    priv->id = 0;
  }
}

/**
 * pinos_daemon_export_uniquely:
 * @daemon: a #PinosDaemon
 * @skel: a #GDBusObjectSkeleton
 *
 * Export @skel with @daemon with a unique name
 *
 * Returns: the unique named used to export @skel.
 */
gchar *
pinos_daemon_export_uniquely (PinosDaemon         *daemon,
                              GDBusObjectSkeleton *skel)
{
  g_return_val_if_fail (PINOS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (G_IS_DBUS_OBJECT_SKELETON (skel), NULL);

  g_dbus_object_manager_server_export_uniquely (daemon->priv->server_manager, skel);

  return g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (skel)));
}

/**
 * pinos_daemon_unexport:
 * @daemon: a #PinosDaemon
 * @object_path: an object path
 *
 * Unexport the object on @object_path
 */
void
pinos_daemon_unexport (PinosDaemon *daemon,
                       const gchar *object_path)
{
  g_return_if_fail (PINOS_IS_DAEMON (daemon));
  g_return_if_fail (g_variant_is_object_path (object_path));

  g_dbus_object_manager_server_unexport (daemon->priv->server_manager, object_path);
}

void
pinos_daemon_add_source (PinosDaemon *daemon,
                         PinosSource *source)
{
  PinosDaemonPrivate *priv;

  g_return_if_fail (PINOS_IS_DAEMON (daemon));
  g_return_if_fail (PINOS_IS_SOURCE (source));
  priv = daemon->priv;

  priv->sources = g_list_prepend (priv->sources, source);
}

void
pinos_daemon_remove_source (PinosDaemon *daemon,
                            PinosSource *source)
{
  PinosDaemonPrivate *priv;

  g_return_if_fail (PINOS_IS_DAEMON (daemon));
  g_return_if_fail (PINOS_IS_SOURCE (source));
  priv = daemon->priv;

  priv->sources = g_list_remove (priv->sources, source);
}

PinosSource *
pinos_daemon_find_source (PinosDaemon *daemon,
                          const gchar *name,
                          GVariant    *props,
                          GBytes      *format_filter,
                          GError      **error)
{
  PinosDaemonPrivate *priv;
  PinosSource *best = NULL;
  GList *walk;

  g_return_val_if_fail (PINOS_IS_DAEMON (daemon), NULL);
  priv = daemon->priv;

  for (walk = priv->sources; walk; walk = g_list_next (walk)) {
    PinosSource *s = walk->data;

    if (name == NULL) {
      best = s;
      break;
    }
    else if (g_str_has_suffix (pinos_source_get_object_path (s), name))
      best = s;
  }

  if (best == NULL) {
    if (error)
      *error = g_error_new (G_IO_ERROR,
                            G_IO_ERROR_NOT_FOUND,
                            "Source not found");
  }
  return best;
}

G_DEFINE_TYPE (PinosDaemon, pinos_daemon, G_TYPE_OBJECT);

static void
pinos_daemon_dispose (GObject * object)
{
  PinosDaemon *daemon = PINOS_DAEMON_CAST (object);

  pinos_daemon_stop (daemon);

  G_OBJECT_CLASS (pinos_daemon_parent_class)->dispose (object);
}

static void
pinos_daemon_finalize (GObject * object)
{
  PinosDaemon *daemon = PINOS_DAEMON_CAST (object);
  PinosDaemonPrivate *priv = daemon->priv;

  g_clear_object (&priv->server_manager);

  G_OBJECT_CLASS (pinos_daemon_parent_class)->finalize (object);
}

static void
pinos_daemon_class_init (PinosDaemonClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (PinosDaemonPrivate));

  gobject_class->dispose = pinos_daemon_dispose;
  gobject_class->finalize = pinos_daemon_finalize;
}

static void
pinos_daemon_init (PinosDaemon * daemon)
{
  PinosDaemonPrivate *priv = daemon->priv = PINOS_DAEMON_GET_PRIVATE (daemon);

  priv->server_manager = g_dbus_object_manager_server_new (PINOS_DBUS_OBJECT_PREFIX);
  priv->senders = g_hash_table_new (g_str_hash, g_str_equal);
}
