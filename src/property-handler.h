/*  Copyright (C) 2009 Itsme S.r.L.
 *  Copyright (C) 2012 Roberto Guido <roberto.guido@linux.it>
 *
 *  This file is part of FSter
 *
 *  FSter is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PROPERTY_HANDLER_H
#define PROPERTY_HANDLER_H

#include "core.h"
#include "property.h"

void        properties_pool_init            ();
void        properties_pool_finish          ();

Property*   properties_pool_get_by_name     (gchar *name);
Property*   properties_pool_get_by_uri      (gchar *uri);

#endif
