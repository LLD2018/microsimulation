/**
 * @file
 * @author  Mark Clements <mark.clements@ki.se>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION 

 Microsimulation of prostate cancer.

 TODO
 * other causes of death - incorporate rates
 * other transitions
 * age-specific reporting of state probabilities
*/

#include "event-r.h"

#include "unuran.h"
#include "unuran_urng_rngstreams.h"

//double inf = 1.0 / 0.0;

using namespace std;

UNUR_GEN   *gen_d;      /* generator objects                      */


//! enum for type of Gleason score
enum gleason_t {nogleason,gleasonLt7,gleason7,gleasonGt7};

//! enum of type of disease stage
enum stage_t {Healthy,Localised,DxLocalised,LocallyAdvanced,DxLocallyAdvanced,
	      Metastatic,DxMetastatic,Death};

//! Class to simulate a person
class Person : public cProcess 
{
public:
  gleason_t gleason;
  stage_t stage;
  bool dx;
  // static members (for statistics)
  static int popSize,               ///< size of the population
    nCancer,                 ///< number of cancers
    nLocalisedCancer,        ///< number of localised cancers diagnosed
    nLocallyAdvancedCancer,  ///< number of locally advanced cancers diagnosed
    nMetastaticCancer;       ///< number of metastatic cancers diagnosed
  static Means personTime;
  void resetPopulation ();
  //
  Person() : dx(false), gleason(nogleason), stage(Healthy) {};
  void init();
  virtual void handleMessage(const cMessage* msg);
  virtual Time age() { return now(); }
};

void Person::resetPopulation() {
  personTime = Means();
  popSize = nCancer = nLocalisedCancer = nLocallyAdvancedCancer = nMetastaticCancer = 0;
}

Means Person::personTime = Means();
int Person::popSize = 0;
int Person::nCancer = 0;
int Person::nLocalisedCancer = 0;
int Person::nLocallyAdvancedCancer = 0;
int Person::nMetastaticCancer = 0;

/** Hazard ratio for diagnosis
    @param stage Disease stage
 */
double dxHR(stage_t stage) {
  // raise error if healthy?
  return stage==Healthy ? -1 : 
    (stage==Localised ? 1.1308 : 
     (stage==LocallyAdvanced ? 0.5900 :1.3147));
}

/** Hazard ratio for progression
    @param gleason Gleason category
 */
double progressionHR(gleason_t gleason) {
  return gleason==gleasonLt7 ? 1 :
      (gleason==gleason7 ? 1.3874 : 1.4027 * 1.3874);
}

/** 
    Initialise a simulation run for an individual
 */
void Person::init() {
  if (runif(0,1)<0.2241) 
    scheduleAt(rweibull(exp(2.3525),64.0218),"Localised");
  scheduleAt(rexp(80),"Death");
}

/** 
    Handle receiving self-messages
 */
void Person::handleMessage(const cMessage* msg) {

  double dwellTime, pDx;

  if (msg->name == "Death") {
    personTime += msg->timestamp;
    popSize += 1;
    Sim::stop_simulation();
  }
  
  else if (msg->name == "PCDeath") {
    // record that this was a PC death prior to diagnosis
    personTime += msg->timestamp;
    popSize += 1;
    Sim::stop_simulation(); 
  }
  
  else if (msg->name == "Localised") {
    stage = Localised;
    gleason = (runif(0,1)<0.6812) ? gleasonLt7 : 
      ((runif(0,1)<0.5016) ? gleason7 : gleasonGt7);
    Time dwellTime = now()+
      rweibullHR(exp(1.0353),19.8617,progressionHR(gleason)*
	       dxHR(stage));
    // now separate out for different transitions
    pDx = 1.1308/(2.1308);
    if (runif(0,1)<pDx) {
      scheduleAt(dwellTime, "DxLocalised");
    }
    else {
      scheduleAt(dwellTime,"LocallyAdvanced");
    }
  }
  
  else if (msg->name == "LocallyAdvanced") {
    stage=LocallyAdvanced;
    nLocallyAdvancedCancer += 1;
    Time dwellTime = now()+
      rweibullHR(exp(1.4404),16.3863,progressionHR(gleason)*
	       dxHR(stage));
    // now separate out for different transitions
    pDx = 0.5900/(1.0+0.5900);
    if (runif(0,1)<pDx) {
      scheduleAt(dwellTime, "DxLocallyAdvanced");
    }
    else {
      scheduleAt(dwellTime,"Metastatic");
    }
  }

  else if (msg->name == "Metastatic") {
    stage=Metastatic;
    Time dwellTime = now()+
      rweibullHR(exp(1.4404),1.4242,progressionHR(gleason)*
	       dxHR(stage));
    // now separate out for different transitions
    pDx = 1.3147/(1.0+1.3147); 
    if (runif(0,1)<pDx) {
      scheduleAt(dwellTime, "DxMetastatic");
    }
    else {
      scheduleAt(dwellTime,"PCDeath"); // prior to diagnosis!
    }
  }

  else if (msg->name == "DxLocalised") {
    dx=true;
    nLocalisedCancer += 1;
    // relative survival
  }

  else if (msg->name == "DxLocallyAdvanced") {
    dx=true;
    nLocallyAdvancedCancer += 1;
    // relative survival
  }

  else if (msg->name == "DxMetastatic") {
    dx=true;
    nMetastaticCancer += 1;
    // relative survival
  };

};

extern "C" {

  void callPersonSimulation(double* parms, int *nin, double *out, int *nout) {
    // input parameters from R (TODO)
    Person person;
    GetRNGstate();
    for (int i = 0; i < *nin; i++) {
      person = Person();
      Sim::create_process(&person);
      Sim::run_simulation();
      Sim::clear();
    }
    // output arguments to R
    out[0] = Person::personTime.mean();
    out[1] = Person::personTime.sd();
    // tidy up -- what needs to be deleted?
    PutRNGstate();
  }
}
