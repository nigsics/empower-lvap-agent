// -*- c-basic-offset: 4; related-file-name: "../include/click/notifier.hh" -*-
/*
 * notifier.{cc,hh} -- activity notification
 * Eddie Kohler
 *
 * Copyright (c) 2002 International Computer Science Institute
 * Copyright (c) 2004-2005 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/notifier.hh>
#include <click/router.hh>
#include <click/element.hh>
#include <click/elemfilter.hh>
#include <click/bitvector.hh>
CLICK_DECLS

// should be const, but we need to explicitly initialize it
atomic_uint32_t NotifierSignal::static_value;
const char Notifier::EMPTY_NOTIFIER[] = "Notifier.EMPTY";
const char Notifier::FULL_NOTIFIER[] = "Notifier.FULL";

/** @file notifier.hh
 * @brief Support for activity signals.
 */

/** @class NotifierSignal
 * @brief An activity signal.
 *
 * Activity signals in Click let one element determine whether another element
 * is active.  For example, consider an element @e X pulling from a @e Queue.
 * If the @e Queue is empty, there's no point in @e X trying to pull from it.
 * Thus, the @e Queue has an activity signal that's active when it contains
 * packets and inactive when it's empty.  @e X can check the activity signal
 * before pulling, and do something else if it's inactive.  Combined with the
 * sleep/wakeup functionality of ActiveNotifier, this can greatly reduce CPU
 * load due to polling.
 *
 * A "basic activity signal" is essentially a bit that's either on or off.
 * When it's on, the signal is active.  NotifierSignal can represent @e
 * derived activity signals as well.  A derived signal combines information
 * about @e N basic signals using the following invariant: If any of the basic
 * signals is active, then the derived signal is also active.  There are no
 * other guarantees; in particular, the derived signal might be active even if
 * @e none of the basic signals are active.
 *
 * Click elements construct NotifierSignal objects in four ways:
 *
 *  - idle_signal() returns a signal that's never active.
 *  - busy_signal() returns a signal that's always active.
 *  - Router::new_notifier_signal() creates a new basic signal.  This method
 *    should be preferred to NotifierSignal's own constructors.
 *  - operator+(NotifierSignal, const NotifierSignal &) creates a derived signal.
 */

/** @class Notifier
 * @brief A basic activity signal and notification provider.
 *
 * The Notifier class represents a basic activity signal associated with an
 * element.  Elements that contain a Notifier object will override
 * Element::cast() to return that Notifier when given the proper name.  This
 * lets other parts of the configuration find the Notifiers.  See
 * upstream_empty_signal() and downstream_full_signal().
 *
 * The ActiveNotifier class, which derives from Notifier, can wake up clients
 * when its activity signal becomes active.
 */

/** @class ActiveNotifier
 * @brief A basic activity signal and notification provider that can
 * reschedule any dependent Task objects.
 *
 * ActiveNotifier, whose base class is Notifier, combines a basic activity
 * signal with the ability to wake up any dependent Task objects when that
 * signal becomes active.  Notifier clients are called @e listeners.  Each
 * listener corresponds to a Task object.  The listener generally goes to
 * sleep -- i.e., becomes unscheduled -- when it runs out of work and the
 * corresponding activity signal is inactive.  The ActiveNotifier class will
 * wake up the listener when it becomes active by rescheduling the relevant
 * Task.
 *
 * Elements that contain ActiveNotifier objects will generally override
 * Element::cast(), allowing other parts of the configuration to find the
 * Notifiers.
 */


/** @brief Initialize the NotifierSignal implementation.
 *
 * This function must be called before NotifierSignal functionality is used.
 * It is safe to call it multiple times.
 *
 * @note Elements don't need to worry about static_initialize(); Click drivers
 * have already called it for you.
 */
void
NotifierSignal::static_initialize()
{
    static_value = TRUE_MASK | OVERDERIVED_MASK;
}

/** @brief Make this signal derived by adding information from @a a.
 * @param a the signal to add
 *
 * Creates a derived signal that combines information from this signal and @a
 * a.  Equivalent to "*this = (*this + @a a)".
 *
 * @sa operator+(NotifierSignal, const NotifierSignal&)
 */
NotifierSignal&
NotifierSignal::operator+=(const NotifierSignal& a)
{
    if (!_mask)
	_value = a._value;

    // preserve busy_signal(); adding other incompatible signals
    // leads to overderived_signal()
    if (*this == busy_signal())
	/* do nothing */;
    else if (a == busy_signal())
	*this = a;
    else if (_value == a._value || !a._mask)
	_mask |= a._mask;
    else
	*this = overderived_signal();
    
    return *this;
}

/** @brief Return a human-readable representation of the signal.
 *
 * Only useful for signal debugging.
 */
String
NotifierSignal::unparse() const
{
    char buf[40];
    sprintf(buf, "%p/%x:%x", _value, _mask, (*_value) & _mask);
    return String(buf);
}


/** @brief Destruct a Notifier. */
Notifier::~Notifier()
{
}

/** @brief Called to register a listener with this Notifier.
 * @param task the listener's Task
 *
 * This notifier should register @a task as a listener, if appropriate.
 * Later, when the signal is activated, the Notifier should reschedule @a task
 * along with the other listeners.  Not all types of Notifier need to provide
 * this functionality, however.  The default implementation does nothing.
 */
int
Notifier::add_listener(Task* task)
{
    (void) task;
    return 0;
}

/** @brief Called to unregister a listener with this Notifier.
 * @param task the listener's Task
 *
 * Undoes the effect of any prior add_listener(@a task).  Should do nothing if
 * @a task was never added.  The default implementation does nothing.
 */
void
Notifier::remove_listener(Task* task)
{
    (void) task;
}

/** @brief Called to register a dependent signal with this Notifier.
 * @param signal the dependent signal
 *
 * This notifier should register @a signal as a dependent signal, if
 * appropriate.  Later, when this notifier's signal is activated, it should go
 * ahead and activate @a signal as well.  Not all types of Notifier need to
 * provide this functionality.  The default implementation does nothing.
 */
int
Notifier::add_dependent_signal(NotifierSignal* signal)
{
    (void) signal;
    return 0;
}

/** @brief Initialize the associated NotifierSignal, if necessary.
 * @param r the associated router
 *
 * Initialize the Notifier's associated NotifierSignal by calling @a r's
 * Router::new_notifier_signal() method, obtaining a new basic activity
 * signal.  Does nothing if the signal is already initialized.
 */
int
Notifier::initialize(Router* r)
{
    if (!_signal.initialized())
	return r->new_notifier_signal(_signal);
    else
	return 0;
}


/** @brief Construct an ActiveNotifier.
 * @param search_op controls notifier path search
 *
 * Constructs an ActiveNotifier object, analogous to the
 * Notifier::Notifier(SearchOp) constructor.  (See that constructor for more
 * informaiton on @a search_op.)
 */
ActiveNotifier::ActiveNotifier(SearchOp search_op)
    : Notifier(search_op), _listener1(0), _listeners(0)
{
}

/** @brief Destroy an ActiveNotifier. */
ActiveNotifier::~ActiveNotifier()
{
    delete[] _listeners;
}

int
ActiveNotifier::listener_change(void *what, int where, bool add)
{
    int n = 0, x;
    task_or_signal_t *tos, *ntos, *otos;

    // common case
    if (!_listener1 && !_listeners && where == 0 && add) {
	_listener1 = (Task *) what;
	return 0;
    }

    for (tos = _listeners, x = 0; tos && x < 2; tos++)
	tos->v ? n++ : x++;
    if (_listener1)
	n++;

    if (!(ntos = new task_or_signal_t[n + 2 + add])) {
      memory_error:
	click_chatter("out of memory in Notifier!");
	return -1;
    }

    otos = ntos;
    if (!_listeners) {
	// handles both the case of _listener1 != 0 and _listener1 == 0
	if (!(_listeners = new task_or_signal_t[3]))
	    goto memory_error;
	_listeners[0].t = _listener1;
	_listeners[1].v = _listeners[2].v = 0;
    }
    for (tos = _listeners, x = 0; x < 2; tos++)
	if (tos->v && (add || tos->v != what)) {
	    (otos++)->v = tos->v;
	    if (tos->v == what)
		add = false;
	} else if (!tos->v) {
	    if (add && where == x)
		(otos++)->v = what;
	    (otos++)->v = 0;
	    x++;
	}
    assert(otos - ntos <= n + 2 + add);

    delete[] _listeners;
    if (!ntos[0].v && !ntos[1].v) {
	_listeners = 0;
	_listener1 = 0;
	delete[] ntos;
    } else if (ntos[0].v && !ntos[1].v && !ntos[2].v) {
	_listeners = 0;
	_listener1 = ntos[0].t;
	delete[] ntos;
    } else {
	_listeners = ntos;
	_listener1 = 0;
    }
    return 0;
}

/** @brief Add a listener to this notifier.
 * @param task the listener to add
 *
 * Adds @a task to this notifier's listener list (the clients interested in
 * notification).  Whenever the ActiveNotifier activates its signal, @a task
 * will be rescheduled.
 */
int
ActiveNotifier::add_listener(Task* task)
{
    return listener_change(task, 0, true);
}

/** @brief Remove a listener from this notifier.
 * @param task the listener to remove
 *
 * Removes @a task from this notifier's listener list (the clients interested
 * in notification).  @a task will not be rescheduled when the Notifier is
 * activated.
 */
void
ActiveNotifier::remove_listener(Task* task)
{
    listener_change(task, 0, false);
}

/** @brief Add a dependent signal to this Notifier.
 * @param signal the dependent signal
 *
 * Adds @a signal as a dependent signal to this notifier.  Whenever the
 * ActiveNotifier activates its signal, @a signal will be activated as well.
 */
int
ActiveNotifier::add_dependent_signal(NotifierSignal* signal)
{
    return listener_change(signal, 1, true);
}

/** @brief Return the listener list.
 * @param[out] v collects listener tasks
 *
 * Pushes all listener Task objects onto the end of @a v.
 */
void
ActiveNotifier::listeners(Vector<Task*>& v) const
{
    if (_listener1)
	v.push_back(_listener1);
    else if (_listeners)
	for (task_or_signal_t* l = _listeners; l->t; l++)
	    v.push_back(l->t);
}


namespace {

class NotifierElementFilter : public ElementFilter { public:
    NotifierElementFilter(const char* name);
    bool check_match(Element *e, bool isoutput, int port);
    Vector<Notifier*> _notifiers;
    NotifierSignal _signal;
    bool _pass2;
    bool _need_pass2;
    const char* _name;
};

NotifierElementFilter::NotifierElementFilter(const char* name)
    : _signal(NotifierSignal::idle_signal()),
      _pass2(false), _need_pass2(false), _name(name)
{
}

bool
NotifierElementFilter::check_match(Element* e, bool isoutput, int port)
{
    if (Notifier* n = (Notifier*) (e->cast(_name))) {
	_notifiers.push_back(n);
	if (!n->signal().initialized())
	    n->initialize(e->router());
	_signal += n->signal();
	Notifier::SearchOp search_op = n->search_op();
	if (search_op == Notifier::SEARCH_CONTINUE_WAKE && !_pass2) {
	    _need_pass2 = true;
	    return true;
	} else
	    return search_op == Notifier::SEARCH_STOP;
	
    } else if (port >= 0) {
	Bitvector flow;
	if (e->port_active(isoutput, port) // went from pull <-> push
	    || (e->port_flow(isoutput, port, &flow), flow.zero())) {
	    _signal = NotifierSignal::busy_signal();
	    return true;
	} else
	    return false;

    } else
	return false;
}

}

/** @brief Calculate and return the NotifierSignal derived from all empty
 * notifiers upstream of element @a e's input @a port, and optionally register
 * @a task as a listener.
 * @param e an element
 * @param port the input port of @a e at which to start the upstream search
 * @param task Task to register as a listener, or null
 * @param dependent_notifier Notifier to register as dependent, or null
 *
 * Searches the configuration upstream of element @a e's input @a port for @e
 * empty @e notifiers.  These notifiers are associated with packet storage,
 * and should be true when packets are available (or likely to be available
 * quite soon), and false when they are not.  All notifiers found are combined
 * into a single derived signal.  Thus, if any of the base notifiers are
 * active, indicating that at least one packet is available upstream, the
 * derived signal will also be active.  Element @a e's code generally uses the
 * resulting signal to decide whether or not to reschedule itself.
 *
 * The returned signal is generally conservative, meaning that the signal
 * is true whenever a packet exists upstream, but the elements that provide
 * notification are responsible for ensuring this.
 *
 * If @a task is nonnull, then @a task becomes a listener for each located
 * notifier.  Thus, when a notifier becomes active (when packets become
 * available), @a task will be rescheduled.
 *
 * If @a dependent_notifier is null, then its signal is registered as a
 * <em>dependent signal</em> on each located upstream notifier.  When
 * an upstream notifier becomes active, @a dependent_notifier's signal is also
 * activated.
 *
 * <h3>Supporting upstream_empty_signal()</h3>
 *
 * Elements that have an empty notifier must override the Element::cast()
 * method.  When passed the @a name Notifier::EMPTY_NOTIFIER, this method
 * should return a pointer to the corresponding Notifier object.
 */
NotifierSignal
Notifier::upstream_empty_signal(Element* e, int port, Task* task, Notifier* dependent_notifier)
{
    NotifierElementFilter filter(EMPTY_NOTIFIER);
    Vector<Element*> v;
    int ok = e->router()->upstream_elements(e, port, &filter, v);

    NotifierSignal signal = filter._signal;

    // maybe run another pass
    if (ok >= 0 && signal != NotifierSignal() && filter._need_pass2) {
	filter._pass2 = true;
	ok = e->router()->upstream_elements(e, port, &filter, v);
    }
    
    // All bets are off if filter ran into a push output. That means there was
    // a regular Queue in the way (for example).
    if (ok < 0 || signal == NotifierSignal())
	return NotifierSignal();

    if (task)
	for (int i = 0; i < filter._notifiers.size(); i++)
	    filter._notifiers[i]->add_listener(task);
    if (dependent_notifier)
	for (int i = 0; i < filter._notifiers.size(); i++)
	    filter._notifiers[i]->add_dependent_signal(&dependent_notifier->_signal);

    return signal;
}

/** @brief Calculate and return the NotifierSignal derived from all full
 * notifiers downstream of element @a e's output @a port, and optionally
 * register @a task as a listener.
 * @param e an element
 * @param port the output port of @a e at which to start the downstream search
 * @param task Task to register as a listener, or null
 * @param dependent_notifier Notifier to register as dependent, or null
 *
 * Searches the configuration downstream of element @a e's output @a port for
 * @e full @e notifiers.  These notifiers are associated with packet storage,
 * and should be true when there is space for at least one packet, and false
 * when there is not.  All notifiers found are combined into a single derived
 * signal.  Thus, if any of the base notifiers are active, indicating that at
 * least one path has available space, the derived signal will also be active.
 * Element @a e's code generally uses the resulting signal to decide whether
 * or not to reschedule itself.
 *
 * If @a task is nonnull, then @a task becomes a listener for each located
 * notifier.  Thus, when a notifier becomes active (when space become
 * available), @a task will be rescheduled.
 *
 * If @a dependent_notifier is null, then its signal is registered as a
 * <em>dependent signal</em> on each located downstream notifier.  When
 * an downstream notifier becomes active, @a dependent_notifier's signal is
 * also activated.
 *
 * In current Click, the returned signal is conservative: if it's inactive,
 * then there is no space for packets downstream.
 *
 * <h3>Supporting downstream_full_signal()</h3>
 *
 * Elements that have a full notifier must override the Element::cast()
 * method.  When passed the @a name Notifier::FULL_NOTIFIER, this method
 * should return a pointer to the corresponding Notifier object.
 */
NotifierSignal
Notifier::downstream_full_signal(Element* e, int port, Task* task, Notifier* dependent_notifier)
{
    NotifierElementFilter filter(FULL_NOTIFIER);
    Vector<Element*> v;
    int ok = e->router()->downstream_elements(e, port, &filter, v);

    NotifierSignal signal = filter._signal;

    // maybe run another pass
    if (ok >= 0 && signal != NotifierSignal() && filter._need_pass2) {
	filter._pass2 = true;
	ok = e->router()->downstream_elements(e, port, &filter, v);
    }
    
    // All bets are off if filter ran into a pull input. That means there was
    // a regular Queue in the way (for example).
    if (ok < 0 || signal == NotifierSignal())
	return NotifierSignal();

    if (task)
	for (int i = 0; i < filter._notifiers.size(); i++)
	    filter._notifiers[i]->add_listener(task);
    if (dependent_notifier)
	for (int i = 0; i < filter._notifiers.size(); i++)
	    filter._notifiers[i]->add_dependent_signal(&dependent_notifier->_signal);

    return signal;
}

CLICK_ENDDECLS
