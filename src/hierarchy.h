/*  Copyright (C) 2009 Itsme S.r.L.
 *
 *  This file is part of Filer
 *
 *  Guglielmo is free software; you can redistribute it and/or modify
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

#ifndef __HIERARCHY_DEFINITIONS_H__
#define __HIERARCHY_DEFINITIONS_H__

#include "core.h"
#include "hierarchy.h"
#include "item-handler.h"
#include "contents-plugin.h"
#include "nodes-cache.h"

#define DUMMY_FILEPATH                      "/tmp/.avfs_dummy_reference"
#define DUMMY_DIRPATH                       "/tmp/.avfs_dummy_folder"
#define FAKE_SAVING_FOLDER                  "/tmp/.avfs_contents"

void                build_hierarchy_tree_from_xml           (xmlDocPtr doc);
void                destroy_hierarchy_tree                  ();
ContentsPlugin*     retrieve_contents_plugin                (gchar *name);

ItemHandler*        verify_exposed_path                     (const gchar *path);
void                replace_hierarchy_node                  (ItemHandler *old_item, ItemHandler *new_item);

TrackerClient*      get_tracker_client                      ();
NodesCache*         get_cache_reference                     ();

#endif
