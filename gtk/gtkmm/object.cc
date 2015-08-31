/* Copyright 1998-2010 The gtkmm Development Team
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <gtkmm/object.h>
#include <gtkmm/private/object_p.h>
#include <glibmm/quark.h>
#include <gtk/gtk.h>


namespace Gtk
{

Object::Object(const Glib::ConstructParams& construct_params)
: Glib::Object(construct_params)
{
   gobject_disposed_ = false;

  _init_unmanage(); //We don't like the GTK+ default memory management - we want to be in control._)
}

Object::Object(GObject* castitem)
: Glib::Object(castitem)
{
  gobject_disposed_ = false;

   _init_unmanage(); //We don't like the GTK+ default memory management - we want to be in control.
}

void Object::_init_unmanage(bool /* is_toplevel = false */)
{
  //GTKMM_LIFECYCLE

  if(gobject_)
  {
    //Glib::Object::Object has already stored a pointer to this C++ instance in the underlying C instance,
    //and connected a callback which will, in turn, call our destroy_notify_(),
    //so will will know if GTK+ disposes of the underlying instance.

    // Most GTK+ objects are floating, by default. This means that the container widget controls their lifetime.
    // We'll change this:
    if(g_object_is_floating (gobject_)) //Top-level Windows and Dialogs can not be manag()ed, so there is no need to do this.
    {
      GLIBMM_DEBUG_REFERENCE(this, gobject_);
      g_object_ref_sink(gobject_); //Stops it from being floating - we will make this optional ( see Gtk::manage() ),

      #ifdef GLIBMM_DEBUG_REFCOUNTING
      g_warning("gtkmm after sink: C++ instance: %p, C instance: %p, refcount=%d\n", (void*)(Glib::ObjectBase*)this, (void*)gobject_, G_OBJECT(gobject_)->ref_count);
      g_warning("    c instance gtype: %s\n", G_OBJECT_TYPE_NAME(gobject_));
      #endif

     referenced_ = true; //Not managed.
    }
    else
    {
       //This widget is already not floating. It's probably already been added to a GTK+ container, and has just had Glib::wrap() called on it.
       //It's not floating because containers call g_object_sink() on child widgets to take control of them.
       //We just ref() it so that we can unref it later.
       //GLIBMM_DEBUG_REFERENCE(this, gobject_);
       //g_object_ref(gobject_);

       //Alternatively, it might be a top-level window (e.g. a Dialog). We would then be doing one too-many refs(),
       //We do an extra unref() in Window::_release_c_instance() to take care of that.

       referenced_ = false; //Managed. We should not try to unfloat GObjects that we did not instantiate.
    }
  }
}

void Object::_release_c_instance()
{
  #ifdef GLIBMM_DEBUG_REFCOUNTING
  g_warning("Gtk::Object::_release_c_instance() this=%p, gobject_=%p\n", (void*)(Glib::ObjectBase*)this, (void*)gobject_);
    if(gobject_)
      g_warning("  gtypename: %s\n", G_OBJECT_TYPE_NAME(gobject_));
  #endif

  cpp_destruction_in_progress_ = true;

  // remove our hook.
  GObject* object = gobj();

  if (object)
  {
    g_assert(G_IS_OBJECT(object));

    //We can't do anything with the gobject_ if it's already been disposed.
    //This prevents us from unref-ing it again, or destroying it again after GTK+ has told us that it has been disposed.
    if (!gobject_disposed_)
    {
      if(referenced_)
      {
        //It's not manage()ed so we just unref to destroy it
        #ifdef GLIBMM_DEBUG_REFCOUNTING
        g_warning("final unref: gtypename: %s, refcount: %d\n", G_OBJECT_TYPE_NAME(object), ((GObject*)object)->ref_count);
        #endif

        GLIBMM_DEBUG_UNREFERENCE(this, object);
        g_object_unref(object);

        //destroy_notify_() should have been called after the final g_object_unref()
        //or g_object_run_dispose(), so gobject_disposed_ could now be true.

        // Note that this is not an issue for GtkWindows,
        // because we use gtk_widget_destroy in Gtk::Window::_release_c_instance() instead.
        //
        //If the C instance still isn't dead then insist, by calling g_object_run_dispose().
        //This is necessary because even a manage()d widget is refed when added to a container.
        // <danielk> That's simply not true.  But references might be taken elsewhere,
        // and g_object_run_dispose() just tells everyone "drop your refs, please!".
        if (!gobject_disposed_)
        {
          #ifdef GLIBMM_DEBUG_REFCOUNTING
          g_warning("Gtk::Object::_release_c_instance(): Calling g_object_run_dispose(): gobject_=%p, gtypename=%s\n", (void*)object, G_OBJECT_TYPE_NAME(object));
          #endif

          g_assert(G_IS_OBJECT(object));
          g_object_run_dispose(object); //Container widgets can respond to this.
        }
      }
      else
      {
        //It's manag()ed, but the coder decided to delete it before deleting its parent.
        //That should be OK because the Container can respond to that.
        #ifdef GLIBMM_DEBUG_REFCOUNTING
        g_warning("Gtk::Object::_release_c_instance(): Calling g_object_run_dispose(): gobject_=%p\n", (void*)gobject_);
        #endif

        if (!gobject_disposed_)
        {
          g_assert(G_IS_OBJECT(object));
          g_object_run_dispose(object);
        }
      }
    }

    //If the GObject still exists, disconnect the C++ wrapper from it.
    //The C++ wrapper is being deleted right now.
    disconnect_cpp_wrapper();

    //Glib::Object::~Object() will not g_object_unref() it too. because gobject_ is now 0.
  }
}

Object::Object(Object&& src) noexcept
: Glib::Object(std::move(src)),
  referenced_(std::move(src.referenced_)),
  gobject_disposed_(std::move(src.gobject_disposed_))
{}

Object& Object::operator=(Object&& src) noexcept
{
  Glib::Object::operator=(std::move(src));
  referenced_ = std::move(src.referenced_);
  gobject_disposed_ = std::move(src.gobject_disposed_);
  return *this;
}


Object::~Object() noexcept
{
  #ifdef GLIBMM_DEBUG_REFCOUNTING
  g_warning("Gtk::Object::~Object() gobject_=%p\n", (void*)gobject_);
  #endif

  //This has probably been called already from Gtk::Object::destroy_(), which is called from derived destructors.
  _release_c_instance();
}

void Object::disconnect_cpp_wrapper()
{
  //GTKMM_LIFECYCLE:

  #ifdef GLIBMM_DEBUG_REFCOUNTING
  g_warning("Gtk::Object::disconnect_cpp_wrapper() this=%p, gobject_=%p\n", (void*)(Glib::ObjectBase*)this, (void*)gobject_);
    if(gobject_)
      g_warning("  gtypename: %s\n", G_OBJECT_TYPE_NAME(gobject_));
  #endif

  if(gobj())
  {
    //Prevent gtk vfuncs and default signal handlers from calling our instance methods:
    g_object_steal_qdata((GObject*)gobj(), Glib::quark_); //It will no longer be possible to get the C++ instance from the C instance.

    //Allow us to prevent generation of a new C++ wrapper during destruction:
    g_object_set_qdata((GObject*)gobj(), Glib::quark_cpp_wrapper_deleted_, (gpointer)true);

    //Prevent C++ instance from using GTK+ object:
    gobject_ = 0;

    //TODO: Disconnect any signals, using gtk methods.
    //We'll have to keep a record of all the connections.
  }
}

void Object::destroy_notify_()
{
  //Overriden.
  //GTKMM_LIFECYCLE

  #ifdef GLIBMM_DEBUG_REFCOUNTING
  g_warning("Gtk::Object::destroy_notify_: this=%p, gobject_=%p\n", (void*)(Glib::ObjectBase*)this, (void*)gobject_);
  if(gobject_)
    g_warning("  gtypename=%s\n", G_OBJECT_TYPE_NAME(gobject_));
  #endif

  //TODO: Remove gobject_disposed_ when we can break ABI.
  //      "if (gobject_disposed_)" can be replaced by "if (gobj())" or "if (gobject_)".
  //Remember that it's been disposed (which only happens once):
  //This also stops us from destroying it again in the destructor when it calls destroy_().
  gobject_disposed_ = true;

  //Actually this function is called when the GObject is finalized, not when it's
  //disposed. Clear the pointer to the GObject, because otherwise it would
  //become a dangling pointer, pointing to a non-existant object.
  gobject_ = 0;

  if(!cpp_destruction_in_progress_) //This function might have been called as a side-effect of destroy_() when it called g_object_run_dispose().
  {
    if (!referenced_) //If it's manage()ed.
    {
      #ifdef GLIBMM_DEBUG_REFCOUNTING
      g_warning("Gtk::Object::destroy_notify_: before delete this.\n");
      #endif
      delete this; //Free the C++ instance.
    }
    else  //It's not managed, but the C gobject_ just died before the C++ instance..
    {
      #ifdef GLIBMM_DEBUG_REFCOUNTING
      g_warning("Gtk::Object::destroy_notify_: setting gobject_ to 0\n");
      #endif
    }
  }

}

void Object::destroy_()
{
  //Called from destructors.
  //GTKMM_LIFECYCLE

  #ifdef GLIBMM_DEBUG_REFCOUNTING
  g_warning("Gtk::Object::destroy_(): gobject_: %p\n", (void*)gobject_);
  if(gobject_)
   g_warning("  gtypename: %s\n", G_OBJECT_TYPE_NAME(gobject_));
  #endif

  if ( !cpp_destruction_in_progress_ )
  {
    cpp_destruction_in_progress_ = true;

    //destroy the C instance:
    _release_c_instance();
  }

  //The C++ destructor will be reached later. This function was called by a destructor.
}

void Object::set_manage()
{
  //GTKMM_LIFECYCLE
  //This object will not be unref()ed by gtkmm, though it could be destroyed if the coder deletes the C++ instance early.
  //This method is overriden in Gtk::Window because they can not be manage()ed.

  if (!referenced_) return; //It's already managed.

  // remove our reference
  if (gobject_->ref_count >= 1) //This should only happen just after creation. We don't use "==1" because GtkButton starts with a refcount of 2 when using a mnemonic.
  {
    //g_warning("Object::set_manage(), making object floating: %s\n", G_OBJECT_TYPE_NAME(gobject_));

    // Cowardly refuse to remove last reference make floating instead. //TODO: What does that comment mean?
    #ifdef GLIBMM_DEBUG_REFCOUNTING
      g_warning("Object::set_manage(): setting GTK_FLOATING: gobject_ = %p", (void*) gobj());
      g_warning("  gtypename=%s\n", G_OBJECT_TYPE_NAME(gobj()));
    #endif
    //deprecated: GTK_OBJECT_SET_FLAGS(gobj(), GTK_FLOATING);
    g_object_force_floating(gobject_);
  }
  else
  {
    g_warning("Object::set_manage(). Refcount seems to be 0. %s\n", G_OBJECT_TYPE_NAME(gobject_));

    //DEF_GLIBMM_DEBUG_UNREF(this, gobj())
    //g_object_unref(gobj());
  }

  //g_warning("Object::set_manage(): end: %s", G_OBJECT_TYPE_NAME(gobject_));
  //g_warning("  refcount=%d", G_OBJECT(gobj())->ref_count);

  referenced_ = false;
}

//TODO: This protected function is not used any more. Can it be removed without breaking ABI/API?
void Object::callback_weak_notify_(void* data, GObject* /* gobject */) //static
{
  //This is only used for a short time, then disconnected.

  Object* cppObject = static_cast<Object*>(data);
  if(cppObject) //This will be 0 if the C++ destructor has already run.
  {
    cppObject->gobject_disposed_ = true;
  }

  //TODO: Do we need to do this?: g_object_weak_unref(gobject, &Object::callback_weak_notify_, data);
}

bool Object::is_managed_() const
{
  return !referenced_;
}

} // namespace Gtk

namespace
{
} // anonymous namespace

namespace Gtk
{


/* The *_Class implementation: */

const Glib::Class& Object_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Class has to know the class init function to clone custom types.
    class_init_func_ = &Object_Class::class_init_function;

    // This is actually just optimized away, apparently with no harm.
    // Make sure that the parent type has been created.
    //CppClassParent::CppObjectType::get_type();

    // Create the wrapper type, with the same class/instance size as the base type.
    register_derived_type(g_object_get_type());

    // Add derived versions of interfaces, if the C type implements any interfaces:

  }

  return *this;
}


void Object_Class::class_init_function(void* g_class, void* class_data)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_class);
  CppClassParent::class_init_function(klass, class_data);


}


Glib::ObjectBase* Object_Class::wrap_new(GObject* o)
{
  return manage(new Object((GObject*)(o)));

}


/* The implementation: */

Object::CppClassType Object::object_class_; // initialize static member

GType Object::get_type()
{
  return object_class_.init().get_type();
}


GType Object::get_base_type()
{
  return g_object_get_type();
}


} // namespace Gtk
