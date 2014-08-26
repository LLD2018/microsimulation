// -*-C++-*-
//
//  This file is part of SSim, a simple discrete-event simulator.
//  See http://www.inf.usi.ch/carzaniga/ssim/
//  
//  Copyright (C) 1998-2005 University of Colorado
//  Copyright (C) 2012 Antonio Carzaniga
//  
//  Authors: Antonio Carzaniga <firstname.lastname@usi.ch>
//           See AUTHORS for full details.
//  
//  SSim is free software: you can redistribute it and/or modify it under
//  the terms of the GNU General Public License as published by the Free
//  Software Foundation, either version 3 of the License, or (at your
//  option) any later version.
//  
//  SSim is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//  
//  You should have received a copy of the GNU General Public License
//  along with SSim.  If not, see <http://www.gnu.org/licenses/>.
//
#include <vector>

#include <siena/ssim.h>
//#include "heap.h"

namespace ssim { 

  namespace Impl {

    class SimImpl  {
    public:

      static void schedule(Sim * sim, Time t, ActionType i, ProcessId p, 
		  const Event * e = 0) throw();
    
      static void schedule_now(Sim * sim, ActionType i, ProcessId p, 
		      const Event * e = 0) throw();

};

    void SimImpl::schedule(Sim * sim, Time t, ActionType i, ProcessId p, 
			 const Event * e) throw() {
	if (e != 0) { 
	    ++(e->refcount); 
	}
	sim->actions.insert(Action(sim->current_time + t, i, p, e ));
    }

    void SimImpl::schedule_now(Sim * sim, ActionType i, ProcessId p, 
			     const Event * e) throw() {
	if (e != 0) { 
	    ++(e->refcount); 
	}
	sim->actions.insert(Action(sim->current_time, i, p, e ));
    }

  } // namespace Impl

  Sim::Sim() : stop_time(INIT_TIME), 
		      current_time(INIT_TIME), 
		      current_process(NULL_PROCESSID),
		      running(false),
		      error_handler(0) {};

  ProcessId Sim::create_process(Process * p) throw() {
    p->sim = this;
    processes.push_back(Impl::PDescr(p));
    ProcessId newpid = processes.size() - 1;
    Impl::SimImpl::schedule_now(this, Impl::A_Init, newpid);
    return newpid;
  }

  void Sim::clear() throw() {
    running = false;
    current_time = INIT_TIME;
    current_process = NULL_PROCESSID;
    processes.clear();
    if (error_handler) error_handler->clear();
    for(Impl::a_table_t::iterator a = actions.begin(); a != actions.end(); ++a) {
	const Event * e = (*a).event;
	if (e != 0 && --(e->refcount) == 0) 
	    delete(e);
    }
    actions.clear();
}

  void Sim::remove_event(EventPredicate pred) throw() {
    Impl::ForwardIterator first = actions.begin();
  Impl::ForwardIterator last = actions.end();
  Impl::ForwardIterator result = first;
  while (first != last) {
    if ((*first).type != Impl::A_Event) {
      *result = *first;
      ++result;
    } else {
      const Event * e = (*first).event;
      if (e != 0 && !pred(e)) {
	*result = *first;
      ++result;
      }
    }
    ++first;
  }
  actions.erase(result, actions.end());
}

//
// this is the simulator main loop.
//
  void Sim::run_simulation() {
    //
    // prevents anyone from re-entering the main loop.  Note that this
    // isn't meant to be thread-safe, it works if some process calls
    // Sim::run_simulation() within their process_event() function.
    //
    static bool lock = false;
    if (lock) return;
    lock = true;
    running = true;

    //
    // while there is at least a scheduled action
    //
    while (running && !actions.empty()) {
	//
	// I'm purposely excluding any kind of checks in this version
	// of the simulator.  
	//
	// I should say something like this:
	// assert(current_time <= (*a).first);
	//
	Impl::Action action = actions.pop_first();
	current_time = action.time;
	if (stop_time != INIT_TIME && current_time > stop_time)
	    break;
	current_process = action.pid;
	//
	// right now I don't check if current_process is indeed a
	// valid process.  Keep in mind that this is the heart of the
	// simulator main loop, therefore efficiency is crucial.
	// Perhaps I should check.  This is somehow a design choice.
	//
	Impl::PDescr & pd = processes[current_process];

	if (pd.terminated) {
	    if (error_handler) 
		error_handler->handle_terminated(current_process, 
						 action.event);
	} else if (current_time < pd.available_at) {
	    if (error_handler) 
		error_handler->handle_busy(current_process, action.event);
	} else {
	    switch (action.type) {
	    case Impl::A_Event:
		pd.process->process_event(action.event);
		break;
	    case Impl::A_Init: 
		pd.process->init(); 
		break;
	    case Impl::A_Stop: 
		pd.process->stop();
		//
		// here we must use processes[current_process] instead
		// of pd since pd.process->stop() might have added or
		// removed processes, and therefore resized the
		// processes vector, rendering pd invalid
		//
		processes[current_process].terminated = true;
		break;
	    default:
		//
		// add paranoia checks/logging here?
		//
		break;
	    }
	    // here we must use processes[current_process] instead of
	    // pd.  Same reason as above. the "processes" vector might
	    // have been modified and, as a consequence, resized.  So,
	    // pd may no longer be considered a valid reference.
	    //
	    processes[current_process].available_at = current_time;
	}

	if (action.event != 0)
	    if (--(action.event->refcount) == 0) 
		delete(action.event);
    }
    lock = false;
    running = false;
}

  void Sim::set_stop_time(Time t) throw() {
    stop_time = t;
}

  void Sim::stop_process() throw() {
    Impl::SimImpl::schedule_now(this, Impl::A_Stop, current_process); 
}

  int Sim::stop_process(ProcessId pid) throw() {
    if (processes[pid].terminated) return -1;
    Impl::SimImpl::schedule_now(this, Impl::A_Stop, pid); 
    return 0;
}

  void Sim::stop_simulation() throw() {
    running = false;
}

  void Sim::advance_delay(Time delay) throw() {
    if (!running) return;
    current_time += delay;
}

  ProcessId Sim::this_process() throw() {
    return current_process;
}

  Time Sim::clock() throw() {
    return current_time;
}

  void Sim::self_signal_event(const Event * e) throw() {
    Impl::SimImpl::schedule_now(this, Impl::A_Event, current_process, e);
}

  void Sim::self_signal_event(const Event * e, Time d) throw() {
    Impl::SimImpl::schedule(this, d, Impl::A_Event, current_process, e);
}

  void Sim::signal_event(ProcessId pid, const Event * e) throw() {
    Impl::SimImpl::schedule_now(this, Impl::A_Event, pid, e);
}

  void Sim::signal_event(ProcessId pid, const Event * e, Time d) throw() {
    Impl::SimImpl::schedule(this, d, Impl::A_Event, pid, e);
}

  void Sim::set_error_handler(SimErrorHandler * eh) throw() {
    error_handler = eh;
}

  ProcessId Sim::create_process(ProcessWithPId * p) throw() {
    if (p->process_id == NULL_PROCESSID) {
      p->process_id = Sim::create_process((Process *) p);
      return p->process_id;
    } else {
      return NULL_PROCESSID;
    }
  }

  ProcessWithPId::ProcessWithPId() throw(): process_id(NULL_PROCESSID) {}

  ProcessId ProcessWithPId::pid() const throw() {
    return process_id;
  }

} // namespace ssim
