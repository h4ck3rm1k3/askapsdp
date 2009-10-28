/// @file
///
/// XXX Notes on program XXX
///
/// @copyright (c) 2008 CSIRO
/// Australia Telescope National Facility (ATNF)
/// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
/// PO Box 76, Epping NSW 1710, Australia
/// atnf-enquiries@csiro.au
///
/// This file is part of the ASKAP software distribution.
///
/// The ASKAP software distribution is free software: you can redistribute it
/// and/or modify it under the terms of the GNU General Public License as
/// published by the Free Software Foundation; either version 2 of the License,
/// or (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
///
/// @author XXX XXX <XXX.XXX@csiro.au>
///
#include <askap_simulations.h>

#include <simulationutilities/Spectrum.h>
#include <sourcefitting/Component.h>

#include <askap/AskapLogging.h>
#include <askap/AskapError.h>

ASKAP_LOGGER(logger, ".spectrum");


namespace askap {

    namespace simulations {

      Spectrum::Spectrum(std::string &line)
      {
	float flux,maj,min,pa;
	std::stringstream ss(line);
	ss >> this->itsRA >> this->itsDec >> flux >> maj >> min >> pa;
	this->itsComponent.setPeak(flux);
	this->itsComponent.setMajor(maj);
	this->itsComponent.setMinor(min);
	this->itsComponent.setPA(pa);
      }

      Spectrum::Spectrum(const Spectrum& s)
      {
	this->itsComponent = s.itsComponent;
      }

    }

}
