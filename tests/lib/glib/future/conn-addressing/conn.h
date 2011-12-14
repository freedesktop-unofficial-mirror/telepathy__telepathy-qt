/*
 * conn.h - header for a connection that implements Conn.I.Addressing
 *
 * Copyright (C) 2011 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2011 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#ifndef __EXAMPLE_ADDRESSING_CONN_H__
#define __EXAMPLE_ADDRESSING_CONN_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection.h>

#include "contacts-conn.h"

G_BEGIN_DECLS

typedef struct _ExampleAddressingConnection ExampleAddressingConnection;
typedef struct _ExampleAddressingConnectionClass ExampleAddressingConnectionClass;
typedef struct _ExampleAddressingConnectionPrivate ExampleAddressingConnectionPrivate;

struct _ExampleAddressingConnectionClass {
    TpTestsContactsConnectionClass parent_class;
};

struct _ExampleAddressingConnection {
    TpTestsContactsConnection parent;

    ExampleAddressingConnectionPrivate *priv;
};

GType example_addressing_connection_get_type (void);

/* TYPE MACROS */
#define EXAMPLE_TYPE_ADDRESSING_CONNECTION \
  (example_addressing_connection_get_type ())
#define EXAMPLE_ADDRESSING_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EXAMPLE_TYPE_ADDRESSING_CONNECTION, \
                              ExampleAddressingConnection))
#define EXAMPLE_ADDRESSING_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EXAMPLE_TYPE_ADDRESSING_CONNECTION, \
                           ExampleAddressingConnectionClass))
#define EXAMPLE_ADDRESSING_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EXAMPLE_TYPE_ADDRESSING_CONNECTION))
#define EXAMPLE_ADDRESSING_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EXAMPLE_TYPE_ADDRESSING_CONNECTION))
#define EXAMPLE_ADDRESSING_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EXAMPLE_TYPE_ADDRESSING_CONNECTION, \
                              ExampleAddressingConnectionClass))

G_END_DECLS

#endif
