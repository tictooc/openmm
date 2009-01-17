/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008 Stanford University and the Authors.           *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "BrookIntegrateVerletStepKernel.h"
#include "BrookStreamInternal.h"

using namespace OpenMM;
using namespace std;

/** 
 * BrookIntegrateVerletStepKernel constructor
 * 
 * @param name                  name of the kernel
 * @param platform              platform
 * @param openMMBrookInterface  OpenMMBrookInterface reference
 * @param system                System reference  
 *
 */

BrookIntegrateVerletStepKernel::BrookIntegrateVerletStepKernel( std::string name, const Platform& platform,
                                  OpenMMBrookInterface& openMMBrookInterface, System& system ) : 
                                  IntegrateVerletStepKernel( name, platform ), _openMMBrookInterface( openMMBrookInterface ), _system( system ){

// ---------------------------------------------------------------------------------------

   // static const std::string methodName      = "BrookIntegrateVerletStepKernel::BrookIntegrateVerletStepKernel";

// ---------------------------------------------------------------------------------------
   
   _brookVerletDynamics   = NULL;
   _brookShakeAlgorithm   = NULL;

   const BrookPlatform brookPlatform = dynamic_cast<const BrookPlatform&> (platform);
   if( brookPlatform.getLog() != NULL ){
      setLog( brookPlatform.getLog() );
   } else {
      _log                = NULL;
   }

}

/** 
 * BrookIntegrateVerletStepKernel destructor
 * 
 */
  
BrookIntegrateVerletStepKernel::~BrookIntegrateVerletStepKernel( ){

// ---------------------------------------------------------------------------------------

   // static const std::string methodName      = "BrookIntegrateVerletStepKernel::~BrookIntegrateVerletStepKernel";

// ---------------------------------------------------------------------------------------

   delete _brookVerletDynamics;
   delete _brookShakeAlgorithm;
   
}

/** 
 * Get log file reference
 * 
 * @return  log file reference
 *
 */

FILE* BrookIntegrateVerletStepKernel::getLog( void ) const {
   return _log;
}

/** 
 * Set log file reference
 * 
 * @param  log file reference
 *
 * @return  DefaultReturnValue
 *
 */

int BrookIntegrateVerletStepKernel::setLog( FILE* log ){
   _log = log;
   return DefaultReturnValue;
}

/** 
 * Initialize the kernel, setting up all parameters related to integrator.
 * 
 * @param system                System reference  
 * @param integrator            VerletIntegrator reference
 *
 */

void BrookIntegrateVerletStepKernel::initialize(  const System& system, const VerletIntegrator& integrator ){

// ---------------------------------------------------------------------------------------

   int printOn                              = 0;
   static const std::string methodName      = "BrookIntegrateVerletStepKernel::initialize";

// ---------------------------------------------------------------------------------------
   
   FILE* log             = getLog();
   int numberOfParticles = system.getNumParticles();

   // masses

   std::vector<double> masses;
   masses.resize( numberOfParticles );

   for( int ii = 0; ii < numberOfParticles; ii++ ){
      masses[ii] = static_cast<double>(system.getParticleMass(ii));
   }

   // constraints

   int numberOfConstraints = system.getNumConstraints();

   std::vector<std::vector<int> > constraintIndicesVector;
   constraintIndicesVector.resize( numberOfConstraints );
   std::vector<double> constraintLengths;
   for( int ii = 0; ii < numberOfConstraints; ii++ ){

      int particle1, particle2;
      double distance;

      system.getConstraintParameters( ii, particle1, particle2, distance );

      constraintIndicesVector[ii].push_back( particle1 );
      constraintIndicesVector[ii].push_back( particle2 );
      constraintLengths.push_back( distance );
   }

   _brookVerletDynamics          = new BrookVerletDynamics( );
   _brookVerletDynamics->setup( masses, getPlatform() );
   _brookVerletDynamics->setLog( log );

   _brookShakeAlgorithm          = new BrookShakeAlgorithm( );
   _brookShakeAlgorithm->setLog( log );
   _brookShakeAlgorithm->setup( masses, constraintIndicesVector, constraintLengths, getPlatform() );

   BrookOpenMMFloat tolerance    = static_cast<BrookOpenMMFloat>( integrator.getConstraintTolerance() );
   _brookShakeAlgorithm->setShakeTolerance( tolerance );
   _brookShakeAlgorithm->setMaxIterations( 40 );

   if( printOn && log ){
      (void) fprintf( log, "%s done w/ setup: particles=%d const=%d\n", methodName.c_str(), numberOfParticles, numberOfConstraints );
      (void) fflush( log );
   }

}

/** 
 * Execute kernel
 * 
 * @param context            OpenMMContextImpl reference
 * @param integrator         VerletIntegrator reference
 *
 */

void BrookIntegrateVerletStepKernel::execute( OpenMMContextImpl& context, const VerletIntegrator& integrator ){

// ---------------------------------------------------------------------------------------

   double epsilon                           = 1.0e-04;
   static const std::string methodName      = "BrookIntegrateVerletStepKernel::execute";

// ---------------------------------------------------------------------------------------

   // for each subsequent call, check if parameters need to be updated due to a change
   // in the step size

   // take step

   double stepSize   = integrator.getStepSize();
   double difference = stepSize - (double) _brookVerletDynamics->getStepSize();
   if( fabs( difference ) > epsilon ){
      _brookVerletDynamics->updateParameters( stepSize );
   }

   _brookVerletDynamics->update( *(_openMMBrookInterface.getParticlePositions()), *(_openMMBrookInterface.getParticleVelocities()),
                                 *(_openMMBrookInterface.getParticleForces()), *_brookShakeAlgorithm );


}
