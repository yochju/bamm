#include <R.h>
#include <Rinternals.h>

//forward function declarations
SEXP getListElement(SEXP list, char *str);
SEXP dtrates(SEXP ephy, SEXP segmat, SEXP tol);
SEXP getMatrixColumn(SEXP matrix, int col);
double getMeanRateExponential(double t1, double t2, double p1, double p2);
double getTimeIntegratedBranchRate(double t1, double t2, double p1, double p2);

SEXP getListElement(SEXP list, char *str)
{
	SEXP elmt = R_NilValue, names = getAttrib(list, R_NamesSymbol);
	int i;
	for (i = 0; i < LENGTH(list); i++)
	{
		if(strcmp(CHAR(STRING_ELT(names, i)), str) == 0)
		{
			elmt = VECTOR_ELT(list, i);
			break;
		}
	}
	return elmt;
}

SEXP getMatrixColumn(SEXP matrix, int col)
{
	int nrow = INTEGER(getAttrib(matrix, R_DimSymbol))[0];
	
	SEXP ret;
	ret = PROTECT(allocVector(REALSXP, nrow));
	
	int i;
	for (i = 0; i < nrow; i++)
	{
		REAL(ret)[i] = REAL(matrix)[i + nrow * col];
	}
	return ret;
}

double getMeanRateExponential(double t1, double t2, double p1, double p2)
{	
	if (p2 == 0) 
	{
		return p1;
	}
	return (p1/p2)*(exp(t2*p2) - exp(t1*p2))/(t2 - t1);
}

double getTimeIntegratedBranchRate(double t1, double t2, double p1, double p2)
{
	if (p2 == 0) 
	{
		return (t2 - t1) * p1;
	}
	return (p1/p2)*(exp(t2*p2) - exp(t1*p2));
}

/***
ephy is the bammdata object, a list holding all the relevant data.
segmat is a matrix where the rows represent branches of a phylogeny
broken up into many small segments.  each row indexes one such segment.
columns 2 and 3 give the starting and ending times of that segment and
column 1 is the node of the phylogeny to which that segement belongs.
tol is a precision parameter used for comparing starting and ending 
times of approximating segments and starting and ending times of branches
or branch segments on the phylogeny.
***/

SEXP dtrates(SEXP ephy, SEXP segmat, SEXP tol)
{
	double eps = REAL(tol)[0];
	
	int k, j, l, nprotect = 0;
	int nsamples = LENGTH(getListElement(ephy, "eventBranchSegs"));
	
	SEXP nodeseg = getMatrixColumn(segmat, 0); nprotect++;	
	SEXP segbegin = getMatrixColumn(segmat, 1); nprotect++;
	SEXP segend = getMatrixColumn(segmat, 2); nprotect++;
	int nsegs = LENGTH(nodeseg);
	
	SEXP rates;
	PROTECT(rates = allocVector(REALSXP, nsegs)); nprotect++;
	
	for(k = 0; k < nsegs; k++)
	{
		REAL(rates)[k] = 0.;
	}
	
	int nrow, node, nnode, event, nxtevent, isGoodStart, isGoodEnd;
	double begin, end, Start, lam1, lam2, relStart, relEnd, rightshift, leftshift, ret;		
	for (k = 0; k < nsamples; k++)
	{
		SEXP eventSegs, eventData;

		eventSegs = PROTECT(VECTOR_ELT(getListElement(ephy, "eventBranchSegs"), k)); nprotect++;
		eventData = PROTECT(VECTOR_ELT(getListElement(ephy, "eventData"), k)); nprotect++;
					
		nrow = INTEGER(getAttrib(eventSegs, R_DimSymbol))[0];
		
		for(j = 0; j < nrow; j++) //move down the rows of eventSegs
		{
			//eventSegs is 4 column matrix, node is in first column stored as double
			node = (int) REAL(eventSegs)[j + nrow * 0];
			
			//event index is in fourth column stored as double
			event = (int) REAL(eventSegs)[j  + nrow * 3];
			
			//begin and end of current branch segment are in second and third columns stored as doubles
			begin = REAL(eventSegs)[j + nrow * 1];
			end = REAL(eventSegs)[j + nrow * 2];
			
			//find next node to later check for shift point on branch
			if (j < nrow)
			{
				nnode = (int) REAL(eventSegs)[(j+1) + nrow * 0];
				nxtevent = (int) REAL(eventSegs)[(j+1) + nrow * 3];
			}
			
			//eventData is dataframe holding event parameters for the current tree
			//need to find the row that corresponds to the event index. in eventData
			//the rows are strictly ordered such that row 0 = event1, row 1 = event2, etc.
			Start = REAL(getListElement(eventData, "time"))[event-1];
			lam1 = REAL(getListElement(eventData, "lam1"))[event-1];
			lam2 = REAL(getListElement(eventData, "lam2"))[event-1];
						
			//need to find which approximating segments match this branch segment
			for (l = 0; l < nsegs; l++)
			{
				if ( (int) REAL(nodeseg)[l] == node)
				{
					//isGoodStart = REAL(segbegin)[l] >= begin;
					isGoodStart = ( (REAL(segbegin)[l] - begin) >= 0. || ( (REAL(segbegin)[l] - begin) < 0. && (REAL(segbegin)[l] - begin) >= -1.*eps) );
					//isGoodEnd = REAL(segend)[l] <= end;
					isGoodEnd =  ( (REAL(segend)[l] - end) <= 0. || ( (REAL(segend)[l] - end) > 0. && (REAL(segend)[l] - end) <= eps) );

					if (isGoodStart && isGoodEnd)
					{					
						relStart = REAL(segbegin)[l] - Start;
						relEnd = REAL(segend)[l] - Start;
						ret = getMeanRateExponential(relStart,relEnd,lam1,lam2);
						
						REAL(rates)[l] += ret/((double) nsamples);
					}
					//check for shift straddle
					if (node == nnode)
					{
						isGoodStart = REAL(segbegin)[l] < end;
						isGoodEnd = REAL(segend)[l] > end;
						if (isGoodStart && isGoodEnd)
						{	
							relStart = REAL(segbegin)[l] - Start;
							relEnd = end - Start;
							leftshift = getTimeIntegratedBranchRate(relStart,relEnd,lam1,lam2);
							
							relStart = 0.;
							relEnd = REAL(segend)[l] - end;
							lam1 = REAL(getListElement(eventData, "lam1"))[nxtevent-1];
							lam2 = REAL(getListElement(eventData, "lam2"))[nxtevent-1];
							rightshift = getTimeIntegratedBranchRate(relStart,relEnd,lam1,lam2);
							
							ret = (leftshift+rightshift)/(REAL(segend)[l] - REAL(segbegin)[l]);
							
							REAL(rates)[l] += ret/((double) nsamples);
						}
					}
				}
				else
				{
					continue;
				}
			}			
		}
		UNPROTECT(2); nprotect -= 2; //protected eventSegs and eventData, which we no longer need
	}
	UNPROTECT(nprotect);
	return rates;
}