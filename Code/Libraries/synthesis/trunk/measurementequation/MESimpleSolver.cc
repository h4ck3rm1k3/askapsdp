#include <measurementequation/MESimpleSolver.h>

#include <iostream>

namespace conrad
{
namespace synthesis
{


void MESimpleSolver::addEquations(const MERegularNormalEquations& normeq) {
	itsRegularEquations.merge(normeq);
}

void MESimpleSolver::addEquations(const MEImageNormalEquations& normeq) {
	itsImageEquations.merge(normeq);
}

void MESimpleSolver::setDesignMatrix(const MERegularDesignMatrix& designmatrix) 
{
	itsMatrix=designmatrix;
}

void MESimpleSolver::init() {
	itsRegularEquations.reset();
	itsImageEquations.reset();
}
bool MESimpleSolver::solve(MEQuality& quality) {
	  uint rank;
	  rank = 0;
	  // Don't know how to template this!
//	  bool solFlag = itsFitter.solveLoop (rank, &(itsParams.regular().values()));
//	
//	  quality.init();
//	  quality.setSolFlag(solFlag);
//	  quality.setRank(rank);
//	  quality.setMu(itsFitter.getWeightedSD());
//	  quality.setStddev(itsFitter.getSD());
//	  quality.setChi(itsFitter.getChi());

};
bool MESimpleSolver::solveImage(MEQuality& q) {
//		for (MEImageParams::iterator iter=itsImageParams.begin();iter!=itsImageParams.end();iter++) {
//			if((*iter).second.isFree()) {
//				casa::LatticeExprNode delta, value;
//				value=(*iter).second.value();
//				delta=(*iter).second.deriv()/(*iter).second.deriv2();
//				casa::LatticeExprNode newvalue(value+gain()*iif((*iter).second.deriv2() > 0.0, 
//					(*iter).second.deriv()/(*iter).second.deriv2(), 0.0));     
//				(*iter).second.setValue(newvalue);
//			}
//		}
	return true;
};

}
}