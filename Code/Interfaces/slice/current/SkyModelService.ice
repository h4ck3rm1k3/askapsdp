// @file SkyModelService.ice
//
// @copyright (c) 2011 CSIRO
// Australia Telescope National Facility (ATNF)
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// PO Box 76, Epping NSW 1710, Australia
// atnf-enquiries@csiro.au
//
// This file is part of the ASKAP software distribution.
//
// The ASKAP software distribution is free software: you can redistribute it
// and/or modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the License,
// or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

#ifndef ASKAP_SKY_MODEL_SERVICE_ICE
#define ASKAP_SKY_MODEL_SERVICE_ICE

#include <CommonTypes.ice>

module askap
{
module interfaces
{
module skymodelservice
{
    /**
     * A component.
     **/
    struct Component
    {
        /**
         * Unique component index number
         **/
        long id;

        /**
         * Right ascension in the J2000 coordinate system
         * Units: degrees
         **/
        double rightAscension;

        /**
         * Declination in the J2000 coordinate system
         * Units: degrees
         **/
        double declination;

        /**
         * Position angle. Counted east from north.
         * Units: radians
         **/
        double positionAngle;

        /**
         * Major axis
         * Units: arcsecs
         **/
        double majorAxis;

        /**
         * Minor axis
         * Units: arcsecs
         **/
        double minorAxis;

        /**
         * Flux at 1400 Mhz
         * Units: Jy (log10 of flux in Jy???)
         **/
        double i1400;
    };

    /**
     * A sequence of component identifiers
     **/
    sequence<long> ComponentIdSeq;

    /**
     * A sequence of Components
     **/
    sequence<Component> ComponentSeq;

    /**
     * Interface to the Sky Model Service.
     **/
    interface ISkyModelService
    {
        /**
         * Cone search method.
         *
         * The cone search does not directly return a sequence of components, which
         * could potentially be very large. Instead a sequence of component ids is
         * returned. This allows the caller to then call getComponents() for a subset
         * of the full component list if it is too large. The idea here is to allow
         * the client access perhaps to be hidden behind an iterator which allows
         * the client to deal with one a smaller (more manageable) subset of the
         * result set at a time.
         *
         * @param right_ascension   the right ascension of the centre of the
         *                          search area (in decimal degrees).
         * @param declination       the declination of the centre of the search
         *                          area (in decimal degrees).
         * @param radius            the search radius in decimal degrees.
         *
         * @return                  a sequence of component identifiers.
         **/
        ComponentIdSeq coneSearch(double rightAscension, double declination, double searchRadius);

        /**
         * Obtain a sequence of components.
         *
         * @param component_ids     a sequence of component identifiers
         *
         * @return                  a sequence of components.
         **/
        ComponentSeq getComponents(ComponentIdSeq componentIds);
    };

};
};
};

#endif
