/*
 * Photos - access, organize and share your photos on GNOME
 * Copyright © 2012 – 2017 Red Hat, Inc.
 * Copyright © 2014 Saurav Agarwalla
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* Based on code from:
 *   + Documents
 */


#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "photos-dlna-renderers-manager.h"
#include "photos-dropdown.h"
#include "photos-header-bar.h"
#include "photos-icons.h"
#include "photos-item-manager.h"
#include "photos-main-toolbar.h"
#include "photos-remote-display-manager.h"
#include "photos-search-context.h"
#include "photos-searchbar.h"
#include "photos-selection-controller.h"
#include "photos-utils.h"


struct _PhotosMainToolbar
{
  GtkBox parent_instance;
  GAction *search;
  GSimpleAction *gear_menu;
  GtkWidget *favorite_button;
  GtkWidget *header_bar;
  GtkWidget *remote_display_button;
  GtkWidget *searchbar;
  GtkWidget *selection_menu;
  PhotosBaseManager *item_mngr;
  PhotosModeController *mode_cntrlr;
  PhotosRemoteDisplayManager *remote_mngr;
  PhotosSelectionController *sel_cntrlr;
};


G_DEFINE_TYPE (PhotosMainToolbar, photos_main_toolbar, GTK_TYPE_BOX);


static void photos_main_toolbar_favorite_button_update (PhotosMainToolbar *self, gboolean favorite);


static gchar *
photos_main_toolbar_create_selection_mode_label (PhotosMainToolbar *self, PhotosBaseItem *active_collection)
{
  GList *selection;
  g_autofree gchar *label = NULL;
  gchar *ret_val = NULL;
  guint length;

  selection = photos_selection_controller_get_selection (self->sel_cntrlr);
  length = g_list_length (selection);
  if (length == 0)
    label = g_strdup(_("Click on items to select them"));
  else
    label = g_strdup_printf (ngettext ("%d selected", "%d selected", length), length);

  if (active_collection != NULL)
    ret_val = g_markup_printf_escaped ("<b>%s</b> (%s)", photos_base_item_get_name (active_collection), label);
  else
    ret_val = g_steal_pointer (&label);

  return ret_val;
}


static void
photos_main_toolbar_set_toolbar_title (PhotosMainToolbar *self)
{
  PhotosBaseItem *active_collection;
  PhotosWindowMode window_mode;
  gboolean selection_mode;
  g_autofree gchar *primary = NULL;

  selection_mode = photos_utils_get_selection_mode ();
  window_mode = photos_mode_controller_get_window_mode (self->mode_cntrlr);

  if (selection_mode)
    {
      g_return_if_fail (window_mode == PHOTOS_WINDOW_MODE_COLLECTION_VIEW
                        || window_mode == PHOTOS_WINDOW_MODE_COLLECTIONS
                        || window_mode == PHOTOS_WINDOW_MODE_FAVORITES
                        || window_mode == PHOTOS_WINDOW_MODE_OVERVIEW
                        || window_mode == PHOTOS_WINDOW_MODE_SEARCH);
    }

  if (window_mode == PHOTOS_WINDOW_MODE_EDIT || window_mode == PHOTOS_WINDOW_MODE_PREVIEW)
    g_return_if_fail (!selection_mode);

  active_collection = photos_item_manager_get_active_collection (PHOTOS_ITEM_MANAGER (self->item_mngr));

  switch (window_mode)
    {
    case PHOTOS_WINDOW_MODE_COLLECTION_VIEW:
      if (selection_mode)
        primary = photos_main_toolbar_create_selection_mode_label (self, active_collection);
      else
        primary = g_strdup (photos_base_item_get_name (active_collection));
      break;

    case PHOTOS_WINDOW_MODE_COLLECTIONS:
    case PHOTOS_WINDOW_MODE_FAVORITES:
    case PHOTOS_WINDOW_MODE_OVERVIEW:
    case PHOTOS_WINDOW_MODE_SEARCH:
      if (selection_mode)
        primary = photos_main_toolbar_create_selection_mode_label (self, NULL);
      break;

    case PHOTOS_WINDOW_MODE_EDIT:
    case PHOTOS_WINDOW_MODE_PREVIEW:
      {
        GObject *item;

        item = photos_base_manager_get_active_object (self->item_mngr);
        if (item != NULL)
          primary = g_strdup (photos_base_item_get_name_with_fallback (PHOTOS_BASE_ITEM (item)));

        break;
      }

    case PHOTOS_WINDOW_MODE_NONE:
    default:
      break;
    }

  if (selection_mode)
    {
      if (primary != NULL)
        {
          GtkWidget *label;

          gtk_button_set_label (GTK_BUTTON (self->selection_menu), primary);
          label = gtk_bin_get_child (GTK_BIN (self->selection_menu));
          gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
        }
    }
  else
    gtk_header_bar_set_title (GTK_HEADER_BAR (self->header_bar), primary);
}


static void
photos_main_toolbar_back_button_clicked (PhotosMainToolbar *self)
{
  photos_mode_controller_go_back (self->mode_cntrlr);
}


static GtkWidget *
photos_main_toolbar_add_back_button (PhotosMainToolbar *self)
{
  GtkWidget *back_button;

  back_button = gtk_button_new_from_icon_name (PHOTOS_ICON_GO_PREVIOUS_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text (back_button, _("Back"));
  gtk_header_bar_pack_start (GTK_HEADER_BAR (self->header_bar), back_button);
  g_signal_connect_swapped (back_button, "clicked", G_CALLBACK (photos_main_toolbar_back_button_clicked), self);

  return back_button;
}


static void
photos_main_toolbar_remote_display_button_clicked (PhotosMainToolbar *self)
{
  photos_remote_display_manager_stop (self->remote_mngr);
}


static void
photos_main_toolbar_add_remote_display_button (PhotosMainToolbar *self)
{
  PhotosDlnaRenderer *renderer;
  GtkLabel *label;
  g_autofree gchar *text = NULL;
  const gchar *name;

  g_clear_pointer (&self->remote_display_button, (GDestroyNotify) gtk_widget_destroy);

  renderer = photos_remote_display_manager_get_renderer (self->remote_mngr);
  name = photos_dlna_renderer_get_friendly_name (renderer);
  text = g_markup_printf_escaped ("Displaying on <b>%s</b>", name);

  self->remote_display_button = gtk_button_new_with_label (text);
  label = GTK_LABEL (gtk_bin_get_child (GTK_BIN (self->remote_display_button)));
  gtk_label_set_ellipsize (label, PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_use_markup (label, TRUE);
  gtk_widget_set_margin_end (self->remote_display_button, 12);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (self->header_bar), self->remote_display_button);
  gtk_widget_show_all (self->remote_display_button);

  g_signal_connect_swapped (self->remote_display_button,
                            "clicked",
                            G_CALLBACK (photos_main_toolbar_remote_display_button_clicked),
                            self);
}


static void
photos_main_toolbar_update_remote_display_button (PhotosMainToolbar *self)
{
  PhotosWindowMode window_mode;
  gboolean selection_mode, active;

  selection_mode = photos_utils_get_selection_mode ();
  window_mode = photos_mode_controller_get_window_mode (self->mode_cntrlr);
  active = photos_remote_display_manager_is_active (self->remote_mngr);

  if (active && !selection_mode && window_mode != PHOTOS_WINDOW_MODE_PREVIEW)
    photos_main_toolbar_add_remote_display_button (self);
  else
    g_clear_pointer (&self->remote_display_button, (GDestroyNotify) gtk_widget_destroy);
}


static void
photos_main_toolbar_item_active_changed (PhotosMainToolbar *self, GObject *object)
{
  PhotosWindowMode window_mode;
  gboolean favorite;

  if (object == NULL)
    return;

  window_mode = photos_mode_controller_get_window_mode (self->mode_cntrlr);
  if (window_mode != PHOTOS_WINDOW_MODE_PREVIEW)
    return;

  photos_main_toolbar_set_toolbar_title (self);

  favorite = photos_base_item_is_favorite (PHOTOS_BASE_ITEM (object));
  photos_main_toolbar_favorite_button_update (self, favorite);
}


static GtkWidget *
photos_main_toolbar_add_search_button (PhotosMainToolbar *self)
{
  GtkWidget *image;
  GtkWidget *search_button;

  image = gtk_image_new_from_icon_name (PHOTOS_ICON_EDIT_FIND_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  search_button = gtk_toggle_button_new ();
  gtk_widget_set_tooltip_text (search_button, _("Search"));
  gtk_button_set_image (GTK_BUTTON (search_button), image);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (search_button), "app.search");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), search_button);

  return search_button;
}


static GtkWidget *
photos_main_toolbar_add_selection_button (PhotosMainToolbar *self)
{
  GtkWidget *selection_button;

  selection_button = gtk_button_new_from_icon_name (PHOTOS_ICON_OBJECT_SELECT_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text (selection_button, _("Select Items"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (selection_button), "app.selection-mode");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), selection_button);

  return selection_button;
}


static void
photos_main_toolbar_clear_state_data (PhotosMainToolbar *self)
{
  g_clear_pointer (&self->remote_display_button, (GDestroyNotify) gtk_widget_destroy);

  if (self->item_mngr != NULL)
    g_signal_handlers_disconnect_by_func (self->item_mngr, photos_main_toolbar_item_active_changed, self);

  if (self->sel_cntrlr != NULL)
    g_signal_handlers_disconnect_by_func (self->sel_cntrlr, photos_main_toolbar_set_toolbar_title, self);
}


static void
photos_main_toolbar_clear_toolbar (PhotosMainToolbar *self)
{
  photos_main_toolbar_clear_state_data (self);
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), FALSE);
  photos_header_bar_clear (PHOTOS_HEADER_BAR (self->header_bar));
  g_simple_action_set_enabled (self->gear_menu, FALSE);
}


static GMenu *
photos_main_toolbar_create_preview_menu (PhotosMainToolbar *self)
{
  GMenu *menu;
  g_autoptr (GtkBuilder) builder = NULL;
  PhotosBaseItem *item;

  builder = gtk_builder_new_from_resource ("/org/gnome/Photos/preview-menu.ui");

  menu = G_MENU (g_object_ref (gtk_builder_get_object (builder, "preview-menu")));
  item = PHOTOS_BASE_ITEM (photos_base_manager_get_active_object (self->item_mngr));
  if (item != NULL)
    {
      const gchar *default_app_name;

      default_app_name = photos_base_item_get_default_app_name (item);
      if (default_app_name != NULL)
        {
          GMenu *section;
          g_autofree gchar *label = NULL;

          section = G_MENU (gtk_builder_get_object (builder, "open-section"));
          g_menu_remove (section, 0);

          label = g_strdup_printf (_("Open with %s"), default_app_name);
          g_menu_prepend (section, label, "app.open-current");
        }
    }

  return menu;
}


static void
photos_main_toolbar_favorite_button_clicked (PhotosMainToolbar *self)
{
  PhotosBaseItem *item;
  gboolean favorite;

  item = PHOTOS_BASE_ITEM (photos_base_manager_get_active_object (self->item_mngr));
  favorite = photos_base_item_is_favorite (item);
  photos_base_item_set_favorite (item, !favorite);

  photos_main_toolbar_favorite_button_update (self, !favorite);
}


static void
photos_main_toolbar_favorite_button_update (PhotosMainToolbar *self, gboolean favorite)
{
  GtkWidget *image;
  g_autofree gchar *favorite_label = NULL;

  if (favorite)
    {
      favorite_label = g_strdup (_("Remove from favorites"));
      image = gtk_image_new_from_icon_name (PHOTOS_ICON_FAVORITE_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
    }
  else
    {
      favorite_label = g_strdup (_("Add to favorites"));
      image = gtk_image_new_from_icon_name (PHOTOS_ICON_NOT_FAVORITE_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
    }

  gtk_button_set_image (GTK_BUTTON (self->favorite_button), image);
  gtk_widget_set_tooltip_text (self->favorite_button, favorite_label);
}


static void
photos_main_toolbar_populate_for_collection_view (PhotosMainToolbar *self)
{
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_STANDALONE);
  photos_main_toolbar_add_back_button (self);
  photos_main_toolbar_add_selection_button (self);
  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_populate_for_collections (PhotosMainToolbar *self)
{
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_NORMAL);
  photos_main_toolbar_add_selection_button (self);
  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_populate_for_edit (PhotosMainToolbar *self)
{
  GtkStyleContext *context;
  GtkWidget *cancel_button;
  GtkWidget *done_button;

  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_STANDALONE);

  cancel_button = gtk_button_new_with_label (_("Cancel"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (cancel_button), "app.edit-cancel");
  gtk_header_bar_pack_start (GTK_HEADER_BAR (self->header_bar), cancel_button);

  done_button = gtk_button_new_with_label (_("Done"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (done_button), "app.edit-done");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), done_button);
  context = gtk_widget_get_style_context (done_button);
  gtk_style_context_add_class (context, "suggested-action");
}


static void
photos_main_toolbar_populate_for_favorites (PhotosMainToolbar *self)
{
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_NORMAL);
  photos_main_toolbar_add_selection_button (self);
  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_populate_for_overview (PhotosMainToolbar *self)
{
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_NORMAL);
  photos_main_toolbar_add_selection_button (self);
  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_populate_for_preview (PhotosMainToolbar *self)
{
  g_autoptr (GMenu) preview_menu = NULL;
  GtkWidget *edit_button;
  GtkWidget *image;
  GtkWidget *menu_button;
  GtkWidget *share_button;
  GApplication *app;
  PhotosBaseItem *item;
  gboolean favorite;
  gboolean remote_display_available;
  GAction *remote_display_action;

  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_STANDALONE);

  photos_main_toolbar_add_back_button (self);

  preview_menu = photos_main_toolbar_create_preview_menu (self);
  image = gtk_image_new_from_icon_name (PHOTOS_ICON_SYSTEM_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  menu_button = gtk_menu_button_new ();
  gtk_actionable_set_action_name (GTK_ACTIONABLE (menu_button), "app.gear-menu");
  gtk_button_set_image (GTK_BUTTON (menu_button), image);
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), G_MENU_MODEL (preview_menu));
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), menu_button);

  g_simple_action_set_enabled (self->gear_menu, TRUE);

  share_button = gtk_button_new_from_icon_name (PHOTOS_ICON_IMAGE_SHARE_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (share_button), "app.share-current");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), share_button);

  edit_button = gtk_button_new_from_icon_name (PHOTOS_ICON_IMAGE_EDIT_SYMBOLIC, GTK_ICON_SIZE_BUTTON);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (edit_button), "app.edit-current");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), edit_button);

  self->favorite_button = gtk_button_new ();
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), self->favorite_button);
  g_signal_connect_swapped (self->favorite_button,
                            "clicked",
                            G_CALLBACK (photos_main_toolbar_favorite_button_clicked),
                            self);

  item = PHOTOS_BASE_ITEM (photos_base_manager_get_active_object (self->item_mngr));
  favorite = photos_base_item_is_favorite (item);
  photos_main_toolbar_favorite_button_update (self, favorite);

  /* Disable the remote-display-current action if the dLeyna services are not
   * available */
  app = g_application_get_default ();
  remote_display_action = g_action_map_lookup_action (G_ACTION_MAP (app), "remote-display-current");
  remote_display_available = photos_dlna_renderers_manager_is_available ();
  g_simple_action_set_enabled (G_SIMPLE_ACTION (remote_display_action), remote_display_available);

  g_signal_connect_object (self->item_mngr,
                           "active-changed",
                           G_CALLBACK (photos_main_toolbar_item_active_changed),
                           self,
                           G_CONNECT_SWAPPED);
}


static void
photos_main_toolbar_populate_for_search (PhotosMainToolbar *self)
{
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_bar), TRUE);
  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_NORMAL);
  photos_main_toolbar_add_selection_button (self);
  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_populate_for_selection_mode (PhotosMainToolbar *self)
{
  GtkWidget *selection_button;

  photos_header_bar_set_mode (PHOTOS_HEADER_BAR (self->header_bar), PHOTOS_HEADER_BAR_MODE_SELECTION);

  selection_button = gtk_button_new_with_label (_("Cancel"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (selection_button), "app.selection-mode");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), selection_button);

  g_signal_connect_object (self->sel_cntrlr,
                           "selection-changed",
                           G_CALLBACK (photos_main_toolbar_set_toolbar_title),
                           self,
                           G_CONNECT_SWAPPED);

  photos_main_toolbar_add_search_button (self);
}


static void
photos_main_toolbar_constructed (GObject *object)
{
  PhotosMainToolbar *self = PHOTOS_MAIN_TOOLBAR (object);

  G_OBJECT_CLASS (photos_main_toolbar_parent_class)->constructed (object);

  photos_main_toolbar_reset_toolbar_mode (self);
}


static void
photos_main_toolbar_dispose (GObject *object)
{
  PhotosMainToolbar *self = PHOTOS_MAIN_TOOLBAR (object);

  photos_main_toolbar_clear_state_data (self);

  g_clear_object (&self->item_mngr);
  g_clear_object (&self->mode_cntrlr);
  g_clear_object (&self->remote_mngr);
  g_clear_object (&self->sel_cntrlr);

  G_OBJECT_CLASS (photos_main_toolbar_parent_class)->dispose (object);
}


static void
photos_main_toolbar_share_changed_cb (PhotosMainToolbar          *self,
                                      PhotosDlnaRenderer         *renderer,
                                      PhotosBaseItem             *item,
                                      PhotosRemoteDisplayManager *remote_mngr)
{
  photos_main_toolbar_update_remote_display_button (self);
}


static void
photos_main_toolbar_share_error_cb (PhotosMainToolbar          *self,
                                    PhotosDlnaRenderer         *renderer,
                                    PhotosBaseItem             *item,
                                    GError                     *error,
                                    PhotosRemoteDisplayManager *remote_mngr)
{
  photos_main_toolbar_update_remote_display_button (self);

  g_warning ("Error sharing item with remote display: %s", error->message);
}


static void
photos_main_toolbar_init (PhotosMainToolbar *self)
{
  GMenu *selection_menu;
  GAction *action;
  GApplication *app;
  g_autoptr (GtkBuilder) builder = NULL;
  PhotosSearchContextState *state;

  gtk_widget_init_template (GTK_WIDGET (self));

  app = g_application_get_default ();
  state = photos_search_context_get_state (PHOTOS_SEARCH_CONTEXT (app));

  self->gear_menu = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app), "gear-menu"));
  self->search = g_action_map_lookup_action (G_ACTION_MAP (app), "search");

  action = g_action_map_lookup_action (G_ACTION_MAP (app), "selection-mode");
  g_signal_connect_object (action,
                           "notify::state",
                           G_CALLBACK (photos_main_toolbar_reset_toolbar_mode),
                           self,
                           G_CONNECT_SWAPPED);

  builder = gtk_builder_new_from_resource ("/org/gnome/Photos/selection-menu.ui");

  selection_menu = G_MENU (gtk_builder_get_object (builder, "selection-menu"));
  self->selection_menu = gtk_menu_button_new ();
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->selection_menu), G_MENU_MODEL (selection_menu));

  photos_header_bar_set_selection_menu (PHOTOS_HEADER_BAR (self->header_bar), GTK_BUTTON (self->selection_menu));

  self->item_mngr = g_object_ref (state->item_mngr);
  self->mode_cntrlr = g_object_ref (state->mode_cntrlr);
  self->sel_cntrlr = photos_selection_controller_dup_singleton ();

  self->remote_mngr = photos_remote_display_manager_dup_singleton ();
  g_signal_connect_object (self->remote_mngr,
                           "share-began",
                           G_CALLBACK (photos_main_toolbar_share_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->remote_mngr,
                           "share-ended",
                           G_CALLBACK (photos_main_toolbar_share_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->remote_mngr,
                           "share-error",
                           G_CALLBACK (photos_main_toolbar_share_error_cb),
                           self,
                           G_CONNECT_SWAPPED);
}


static void
photos_main_toolbar_class_init (PhotosMainToolbarClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  object_class->constructed = photos_main_toolbar_constructed;
  object_class->dispose = photos_main_toolbar_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Photos/main-toolbar.ui");
  gtk_widget_class_bind_template_child (widget_class, PhotosMainToolbar, header_bar);
  gtk_widget_class_bind_template_child (widget_class, PhotosMainToolbar, searchbar);
}


GtkWidget *
photos_main_toolbar_new (void)
{
  return g_object_new (PHOTOS_TYPE_MAIN_TOOLBAR, NULL);
}


PhotosSearchbar *
photos_main_toolbar_get_searchbar (PhotosMainToolbar *self)
{
  return PHOTOS_SEARCHBAR (self->searchbar);
}


gboolean
photos_main_toolbar_handle_event (PhotosMainToolbar *self, GdkEventKey *event)
{
  gboolean ret_val = FALSE;

  if (!g_action_get_enabled (self->search))
      goto out;

  ret_val = photos_searchbar_handle_event (PHOTOS_SEARCHBAR (self->searchbar), event);

 out:
  return ret_val;
}


gboolean
photos_main_toolbar_is_focus (PhotosMainToolbar *self)
{
  return photos_searchbar_is_focus (PHOTOS_SEARCHBAR (self->searchbar));
}


void
photos_main_toolbar_reset_toolbar_mode (PhotosMainToolbar *self)
{
  gboolean selection_mode;

  photos_main_toolbar_clear_toolbar (self);
  selection_mode = photos_utils_get_selection_mode ();

  if (selection_mode)
    photos_main_toolbar_populate_for_selection_mode (self);
  else
    {
      PhotosWindowMode window_mode;

      window_mode = photos_mode_controller_get_window_mode (self->mode_cntrlr);

      switch (window_mode)
        {
        case PHOTOS_WINDOW_MODE_COLLECTION_VIEW:
          photos_main_toolbar_populate_for_collection_view (self);
          break;

        case PHOTOS_WINDOW_MODE_COLLECTIONS:
          photos_main_toolbar_populate_for_collections (self);
          break;

        case PHOTOS_WINDOW_MODE_EDIT:
          photos_main_toolbar_populate_for_edit (self);
          break;

        case PHOTOS_WINDOW_MODE_FAVORITES:
          photos_main_toolbar_populate_for_favorites (self);
          break;

        case PHOTOS_WINDOW_MODE_OVERVIEW:
          photos_main_toolbar_populate_for_overview (self);
          break;

        case PHOTOS_WINDOW_MODE_PREVIEW:
          photos_main_toolbar_populate_for_preview (self);
          break;

        case PHOTOS_WINDOW_MODE_SEARCH:
          photos_main_toolbar_populate_for_search (self);
          break;

        case PHOTOS_WINDOW_MODE_NONE:
        default:
          break;
        }
    }

  photos_main_toolbar_update_remote_display_button (self);
  photos_main_toolbar_set_toolbar_title (self);
  gtk_widget_show_all (self->header_bar);
}


void
photos_main_toolbar_set_stack (PhotosMainToolbar *self, GtkStack *stack)
{
  photos_header_bar_set_stack (PHOTOS_HEADER_BAR (self->header_bar), stack);
}
