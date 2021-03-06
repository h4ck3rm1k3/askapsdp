/// @file
///
/// Provides generic methods for pattern matching
///
/// @copyright (c) 2007 CSIRO
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
/// @author Matthew Whiting <matthew.whiting@csiro.au>
///
#include <askap_analysis.h>

#include <askap/AskapLogging.h>
#include <askap/AskapError.h>

#include <patternmatching/Triangle.h>
#include <patternmatching/Point.h>
#include <patternmatching/Side.h>

#include <iostream>
#include <math.h>
#include <map>
#include <vector>
#include <utility>
#include <string>

///@brief Where the log messages go.
ASKAP_LOGGER(logger, ".matching");

namespace askap {
  namespace analysis {

    namespace matching {


      Triangle::Triangle()
      {
	this->initialise();
      }

      Triangle::Triangle(Point &a, Point &b, Point &c)
      {
	this->initialise();
	this->define(a, b, c);
      }

      Triangle::Triangle(double x1, double y1, double x2, double y2, double x3, double y3)
      {
	this->initialise();
	Point pt1(x1, y1);
	Point pt2(x2, y2);
	Point pt3(x3, y3);
	this->define(pt1, pt2, pt3);
      }

      void Triangle::initialise()
      {
	this->itsPts = std::vector<Point>(3); 
	this->itIsClockwise = true;
	this->itsLogPerimeter = 0.;
	this->itsRatio = 0.;
	this->itsRatioTolerance = 0.;
	this->itsAngle = 0.;
	this->itsAngleTolerance = 0.;

      }

      //**************************************************************//

      Triangle::Triangle(const Triangle& t) 
      {
	this->operator=(t);
      }

      Triangle& Triangle::operator= (const Triangle& t)
      {
	if (this == &t) return *this;

	this->itsLogPerimeter = t.itsLogPerimeter;
	this->itIsClockwise = t.itIsClockwise;
	this->itsRatio = t.itsRatio;
	this->itsRatioTolerance = t.itsRatioTolerance;
	this->itsAngle = t.itsAngle;
	this->itsAngleTolerance = t.itsAngleTolerance;
	this->itsPts = t.itsPts;
	return *this;
      }

      //**************************************************************//

      void Triangle::define(Point &a, Point &b, Point &c)
      {
	/// @details Define a triangle from three points. The key part
	///  of this function is to order the sides by their
	///  length. The triangle is defined on the basis of the ratio
	///  of the longest to smallest sides, and the angle between
	///  them. The given points are used to define sides, which
	///  are then ordered according to their length. The triangle
	///  parameters are then calculated from the known side
	///  parameters.
	std::vector<Point> ptslist(3);
	ptslist[0] = a;
	ptslist[1] = b;
	ptslist[2] = c;
	std::vector<Side> sides(3);
	sides[0].define(a, b);
	sides[1].define(b, c);
	sides[2].define(c, a);
	std::vector<short> vote(3);

	for (int i = 0; i < 3; i++) {
	  if ((min_element(sides.begin(), sides.end()) - sides.begin()) == i) vote[i] = 1;
	  else if ((max_element(sides.begin(), sides.end()) - sides.begin()) == i) vote[i] = 3;
	  else vote[i] = 2;
	}

	short matrix[3][3];

	for (int i = 0; i < 3; i++) matrix[i][i] = 0;

	matrix[1][0] = matrix[0][1] = vote[0];
	matrix[2][1] = matrix[1][2] = vote[1];
	matrix[0][2] = matrix[2][0] = vote[2];

	for (int i = 0; i < 3; i++) {
	  int sum = 0;

	  for (int j = 0; j < 3; j++) sum += matrix[i][j];

	  switch (sum) {
	  case 4: this->itsPts[0] = ptslist[i]; break;
	  case 3: this->itsPts[1] = ptslist[i]; break;
	  case 5: this->itsPts[2] = ptslist[i]; break;
	  }
	}

	std::sort(sides.begin(), sides.end());
	// the sides are now ordered, so that the first is the shortest
	// use terminology from Groth 1986, where r2=shortest side, r3=longest side
	double r2 = sides.begin()->length(), r3 = sides.rbegin()->length();
	double dx2 = sides.begin()->run(),    dx3 = sides.rbegin()->run();
	double dy2 = sides.begin()->rise(),   dy3 = sides.rbegin()->rise();
	this->itsRatio = r3 / r2;
	this->itsAngle = (dx3 * dx2 + dy3 * dy2) / (r3 * r2);
	double sum = 0.;

	for (int i = 0; i < 3; i++) sum += sides[i].length();

	this->itsLogPerimeter = log10(sum);
	double tantheta = (dy2 * dx3 - dy3 * dx2) / (dx2 * dx3 + dy2 * dy3);
	this->itIsClockwise = (tantheta > 0.);
	defineTolerances();
      }

      //**************************************************************//

      void Triangle::defineTolerances(double epsilon)
      {
	/// @details The tolerances for the triangle parameters are
	/// calculated. These require the angle and ratio parameters
	/// to have been calculated, so this should be done after the
	/// triangle is defined.
	/// @param epsilon The parameter governing the size of the
	/// acceptable error in matching. This defaults to the value
	/// of posTolerance
	Side side1_2(itsPts[0].x() - itsPts[1].x(), itsPts[0].y() - itsPts[1].y());
	Side side1_3(itsPts[0].x() - itsPts[2].x(), itsPts[0].y() - itsPts[2].y());
	double r2 = side1_2.length(), r3 = side1_3.length();
	double sinthetaSqd = 1. - this->itsAngle * this->itsAngle;
	double factor = 1. / (r3 * r3) - this->itsAngle / (r3 * r2) + 1. / (r2 * r2);
	this->itsRatioTolerance = 2. * this->itsRatio * this->itsRatio * epsilon * epsilon * factor;
	this->itsAngleTolerance = 2. * sinthetaSqd * epsilon * epsilon * factor +
	  3. * this->itsAngle * this->itsAngle * pow(epsilon, 4) * factor * factor;
      }

      //**************************************************************//

      bool Triangle::isMatch(Triangle &comp, double epsilon)
      {
	/// @details Does the triangle match another. Compares the
	/// ratios and angles to see whether they match to within the
	/// respective tolerances. Triangle::defineTolerances is
	/// called prior to testing, using the value of epsilon.
	/// @param comp The comparison triangle
	/// @param epsilon The error parameter used to define the
	/// tolerances. Defaults to posTolerance.
	/// @return True if triangles match
	this->defineTolerances(epsilon);
	comp.defineTolerances(epsilon);
	// ASKAPLOG_DEBUG_STR(logger, "this: ratio="<<itsRatio<<", rTol="<<itsRatioTolerance<<", angle="<<itsAngle<<", aTol="<<itsAngleTolerance);
	// ASKAPLOG_DEBUG_STR(logger, "comp: ratio="<<comp.ratio()<<", rTol="<<comp.ratioTol()<<", angle="<<comp.angle()<<", aTol="<<comp.angleTol());
	double ratioSep = this->itsRatio - comp.ratio();
	ratioSep *= ratioSep;
	double ratioTol = this->itsRatioTolerance + comp.ratioTol();
	double angleSep = this->itsAngle - comp.angle();
	angleSep *= angleSep;
	double angleTol = this->itsAngleTolerance + comp.angleTol();
	return ((ratioSep < ratioTol) && (angleSep < angleTol));
      }

      //**************************************************************//
      //**************************************************************//

      std::vector<Triangle> getTriList(std::vector<Point> &pixlist)
      {
	/// @details Create a list of triangles from a list of Points.
	std::vector<Triangle> triList;
	int npix = pixlist.size();

	for (int i = 0; i < npix - 2; i++) {
	  for (int j = i + 1; j < npix - 1; j++) {
	    for (int k = j + 1; k < npix; k++) {
	      Triangle tri(pixlist[i], pixlist[j], pixlist[k]);

	      if (tri.ratio() < 10.) triList.push_back(tri);
	    }
	  }
	}

	ASKAPLOG_INFO_STR(logger, "Generated a list of " << triList.size() << " triangles");
	return triList;
      }

      //**************************************************************//

      std::vector<std::pair<Triangle, Triangle> >
      matchLists(std::vector<Triangle> &list1, std::vector<Triangle> &list2, double epsilon)
      {
	/// @details Finds a list of matching triangles from two
	/// lists. The lists are both sorted in order of increasing
	/// ratio, and the maximum ratio tolerance is found for each
	/// list. Triangles from list1 are compared with a range from
	/// list2, where the ratio of the comparison triangle falls
	/// between the maximum acceptable range using the maximum
	/// ratio tolerances (so that we don't look at every possible
	/// triangle pair). The matching triangles are returned as a
	/// vector of pairs of triangles.
	/// @param list1 The first list of triangles
	/// @param list2 The other list of triangles
	/// @param epsilon The error parameter used to define the tolerances. Defaults to posTolerance.
	/// @return A list of matching pairs of triangles.

	  ASKAPLOG_INFO_STR(logger, "Commencing match between lists of size " << list1.size() << " and " << list2.size());

	size_t size1 = list1.size(), size2 = list2.size();
	// sort in order of increasing ratio
	std::stable_sort(list1.begin(), list1.end());
	std::stable_sort(list2.begin(), list2.end());
	// find maximum ratio tolerances for each list
	double maxTol1=0.,maxTol2=0.;

	for (size_t i = 0; i < size1; i++) {
	  list1[i].defineTolerances(epsilon);

	  if (i==0 || list1[i].ratioTol() > maxTol1) maxTol1 = list1[i].ratioTol();
	}

	for (size_t i = 0; i < size2; i++) {
	  list2[i].defineTolerances(epsilon);

	  if (i==0 || list2[i].ratioTol() > maxTol2) maxTol2 = list2[i].ratioTol();
	}

	// std::vector<bool> matches(size1*size2, false);
	int nmatch = 0;
	std::vector<std::pair<Triangle, Triangle> > matchList;

	// loop over the lists, finding matches
	for (size_t i = 0; i < size1; i++) {
	  double maxRatioB = list1[i].ratio() + sqrt(maxTol1 + maxTol2);
	  double minRatioB = list1[i].ratio() - sqrt(maxTol1 + maxTol2);
//	  ASKAPLOG_DEBUG_STR(logger, "Finding matches for triangle " << list1[i] <<", using epsilon="<<epsilon);
	  std::vector<Triangle> matches;
	  for (size_t j = 0; j < size2 && list2[j].ratio() < maxRatioB; j++) {
	      // for (size_t j = 0; j < size2; j++) {
	    // if (list2[j].ratio() > minRatioB)
	    //   matches[i+j*size1] = list1[i].isMatch(list2[j], epsilon);

	    // if (matches[i+j*size1]){
	      if(list2[j].ratio() > minRatioB && list1[i].isMatch(list2[j], epsilon) ){
		  nmatch++;
		  std::pair<Triangle, Triangle> match(list1[i], list2[j]);
		  matchList.push_back(match);
//		matches.push_back(list2[j]);
		  
	    }
	  }
	  
	}

	ASKAPLOG_INFO_STR(logger, "Number of matching triangles = " << nmatch);

	return matchList;
      }

      //**************************************************************//

      void trimTriList(std::vector<std::pair<Triangle, Triangle> > &trilist)
      {
	/// @details A list of triangle matches is trimmed of false
	/// matches. First, the magnifications (the difference in the
	/// log(perimeter) values of the two matching triangles) are
	/// examined: the true matches will have mags in a small range
	/// of values, while false matches will have a broader
	/// distribution. Only those matches in a narrow range of mags
	/// will be accepted: those with mean_mag +- rms_mag*scale,
	/// where scale is determined based on the number of same- and
	/// opposite-sense matches.
	///
	/// If n_same and n_opp are the numbers of matches with the
	/// same sense (both clockwise or both anticlockwise) or
	/// opposite sense, then we get estimates of the number of
	/// true & false matches by m_t=|n_same-n_opp| and m_f =
	/// n_same + n_opp - m_t. Then scale is :
	/// @li 1 if m_f > m_t
	/// @li 3 if 0.1 m_t > m_f
	/// @li 2 otherwise
	///
	/// Finally, all matches should have the same sense, so if
	/// n_same > n_opp, all opposite sense matches are discarded,
	/// and vice versa.
	unsigned int nIter = 0;
	unsigned int nSame = 0, nOpp = 0;
	const unsigned int maxIter = 5;

	do {

	    double mean = 0., rms = 0., mag,sumx=0.,sumxx=0.;
	    size_t size = trilist.size();

	  for (unsigned int i = 0; i < size; i++) {
	    mag = trilist[i].first.perimeter() - trilist[i].second.perimeter();
	    sumx += mag;
	    sumxx += (mag*mag);
	    if (trilist[i].first.isClockwise() == trilist[i].second.isClockwise()) nSame++;
	    else nOpp++;
	  }

	  mean = sumx/double(size);
	  rms = sqrt(sumxx/double(size) - mean*mean);

	  double trueOnFalse = abs(nSame - nOpp) / double(nSame + nOpp - abs(nSame - nOpp));
	  double scale;

	  if (trueOnFalse < 1.) scale = 1.;
	  else if (trueOnFalse > 10.) scale = 3.;
	  else scale = 2.;

	  ASKAPLOG_DEBUG_STR(logger, "Iteration #"<<nIter << ": meanMag="<<mean<<", rmsMag="<<rms << ", scale="<<scale);
	  
	  std::vector<std::pair<Triangle, Triangle> > newlist;
	  for(size_t i=0;i<trilist.size();i++){
	    mag = trilist[i].first.perimeter() - trilist[i].second.perimeter();
	    if (fabs((mag - mean) / rms) < scale) {
		newlist.push_back(trilist[i]);
	    }
	  }
	  trilist=newlist;
	  ASKAPLOG_DEBUG_STR(logger, "List size now " << trilist.size());

	  nIter++;
	} while (nIter < maxIter && trilist.size() > 0);

	for (unsigned int i = 0; i < trilist.size(); i++) {
	  if (trilist[i].first.isClockwise() == trilist[i].second.isClockwise()) nSame++;
	  else nOpp++;
	}
	
	std::vector<std::pair<Triangle, Triangle> > newlist;
	for(size_t i=0;i<trilist.size();i++){
	    if ( ( (nSame <= nOpp) || (trilist[i].first.isClockwise() == trilist[i].second.isClockwise())) &&
		   ( (nOpp <= nSame) || (trilist[i].first.isClockwise() != trilist[i].second.isClockwise())) )
		newlist.push_back(trilist[i]);
	}
	trilist = newlist;

      }

      //**************************************************************//

      std::vector<std::pair<Point, Point> > vote(std::vector<std::pair<Triangle, Triangle> > &trilist)
      {
	/// @details The final step in removing false matches is the
	/// voting. Each matched triangle votes for matched
	/// points. The array of votes is ordered from max vote to min
	/// vote. If no pair of points received more than one vote,
	/// the lists don't match. Otherwise, successive points are
	/// accepted until one of :
	/// @li The vote drops by a factor of 2
	/// @li We try to accept a point already accepted
	/// @li The vote drops to zero.

	std::multimap<int, std::pair<Point, Point> > voteList;
	std::vector<std::pair<Point,Point> > pts;
	std::vector<int> votes;
	std::multimap<int, std::pair<Point, Point> >::iterator vote;
	std::multimap<int, std::pair<Point, Point> >::reverse_iterator rvote;

	for (unsigned int i = 0; i < trilist.size(); i++) {
	  std::vector<Point> ptlist1 = trilist[i].first.getPtList();
	  std::vector<Point> ptlist2 = trilist[i].second.getPtList();

	  for (int p = 0; p < 3; p++) { // for each of the three points:
	    bool foundMatch=false;
	    if (votes.size()>0){

		for(size_t i=0;i<votes.size() && !foundMatch; i++){
		     if ((pts[i].first.ID() == ptlist1[p].ID()) && (pts[i].second.ID() == ptlist2[p].ID())){
			votes[i]++;
			foundMatch=true;
		    }
		}

	    }
	  
	    if(!foundMatch){
		votes.push_back(1);
		pts.push_back(std::pair<Point,Point>(ptlist1[p],ptlist2[p]));
	    }

	  }
	}

	for(size_t i=0;i<votes.size();i++) voteList.insert( std::pair<int, std::pair<Point, Point> >(votes[i],pts[i]));

	std::vector<std::pair<Point, Point> > outlist;

	if (voteList.rbegin()->first == 1) // largest vote was 1 -- no match;
	  return outlist;

	bool stop = false;
	int prevVote = voteList.rbegin()->first;

	for (rvote = voteList.rbegin(); rvote != voteList.rend() && !stop; rvote++) {

	  for (unsigned int i = 0; i < outlist.size() && !stop; i++) {
	      stop = ( (rvote->second.first.ID() == outlist[i].first.ID()) );
	  }

	  if (rvote != voteList.rbegin()) stop = stop || (rvote->first < 0.5 * prevVote);

	  if (!stop) outlist.push_back(rvote->second);

	  prevVote = rvote->first;
	}

	return outlist;
      }


    }
  }
}
