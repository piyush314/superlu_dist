/*! \file
Copyright (c) 2003, The Regents of the University of California, through
Lawrence Berkeley National Laboratory (subject to receipt of any required
approvals from U.S. Dept. of Energy)

All rights reserved.

The source code is distributed under BSD license, see the file License.txt
at the top-level directory.
*/


/*! @file
 * \brief Solves a system of distributed linear equations A*X = B with a
 * general N-by-N matrix A using the LU factors computed previously.
 *
 * <pre>
 * -- Distributed SuperLU routine (version 6.1) --
 * Lawrence Berkeley National Lab, Univ. of California Berkeley.
 * October 15, 2008
 * September 18, 2018  version 6.0
 * February 8, 2019  version 6.1.1
 * </pre>
 */
#include <math.h>				 
#include "superlu_ddefs.h"
#define ISEND_IRECV


int_t trs_B_init3d(int_t nsupers, double* x, int nrhs, dLUstruct_t * LUstruct, 
	gridinfo3d_t *grid3d)
{

	gridinfo_t * grid = &(grid3d->grid2d);
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* ilsum = Llu->ilsum;
	int_t* xsup = Glu_persist->xsup;
	int_t iam = grid->iam;
	int_t myrow = MYROW( iam, grid );
	int_t mycol = MYCOL( iam, grid );

	for (int_t k = 0; k < nsupers; ++k)
	{
		/* code */
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (myrow == krow && mycol == kcol)
		{
			int_t lk = LBi(k, grid);
			int_t ii = X_BLK (lk);
			int_t knsupc = SuperSize(k);
			MPI_Bcast( &x[ii - XK_H], knsupc * nrhs + XK_H, MPI_DOUBLE, 0, grid3d->zscp.comm);

		}
	}

	return 0;
}

int_t trs_X_gather3d(double* x, int nrhs, dtrf3Dpartition_t*  trf3Dpartition,
                     dLUstruct_t* LUstruct,
                     gridinfo3d_t* grid3d )

{
	int_t maxLvl = log2i(grid3d->zscp.Np) + 1;
	int_t myGrid = grid3d->zscp.Iam;
	int_t* myZeroTrIdxs = trf3Dpartition->myZeroTrIdxs;

	for (int_t ilvl = 0; ilvl < maxLvl - 1; ++ilvl)
	{
		int_t sender, receiver;
		if (!myZeroTrIdxs[ilvl])
		{
			if ((myGrid % (1 << (ilvl + 1))) == 0)
			{
				sender = myGrid + (1 << ilvl);
				receiver = myGrid;
			}
			else
			{
				sender = myGrid;
				receiver = myGrid - (1 << ilvl);
			}
			for (int_t alvl = 0; alvl <= ilvl; alvl++)
			{
				int_t diffLvl  = ilvl - alvl;
				int_t numTrees = 1 << diffLvl;
				int_t blvl = maxLvl - alvl - 1;
				int_t st = (1 << blvl) - 1 + (sender >> alvl);

				for (int_t tr = st; tr < st + numTrees; ++tr)
				{
					/* code */
					gatherSolvedX3d(tr, sender, receiver, x, nrhs,  trf3Dpartition, LUstruct, grid3d);
				}
			}

		}
	}

	return 0;
}


int_t gatherSolvedX3d(int_t treeId, int_t sender, int_t receiver, double* x, int nrhs,
                      dtrf3Dpartition_t*  trf3Dpartition, dLUstruct_t* LUstruct, gridinfo3d_t* grid3d )
{
	sForest_t** sForests = trf3Dpartition->sForests;
	sForest_t* sforest = sForests[treeId];
	if (!sforest) return 0;
	int_t nnodes = sforest->nNodes ;
	int_t *nodeList = sforest->nodeList ;

	gridinfo_t * grid = &(grid3d->grid2d);
	int_t myGrid = grid3d->zscp.Iam;
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* ilsum = Llu->ilsum;
	int_t* xsup = Glu_persist->xsup;
	int_t iam = grid->iam;
	int_t myrow = MYROW( iam, grid );
	int_t mycol = MYCOL( iam, grid );


	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (myrow == krow && mycol == kcol)
		{
			int_t lk = LBi(k, grid);
			int_t ii = X_BLK (lk);
			int_t knsupc = SuperSize(k);
			if (myGrid == sender)
			{
				/* code */
				MPI_Send( &x[ii], knsupc * nrhs, MPI_DOUBLE, receiver, k,  grid3d->zscp.comm);
			}
			else
			{
				MPI_Status status;
				MPI_Recv( &x[ii], knsupc * nrhs, MPI_DOUBLE, sender, k, grid3d->zscp.comm, &status );
			}
		}
	}

	return 0;
}


int_t fsolveReduceLsum3d(int_t treeId, int_t sender, int_t receiver, double* lsum, double* recvbuf, int nrhs,
                         dtrf3Dpartition_t*  trf3Dpartition, dLUstruct_t* LUstruct, gridinfo3d_t* grid3d ,
                         xtrsTimer_t *xtrsTimer)
{
	sForest_t** sForests = trf3Dpartition->sForests;
	sForest_t* sforest = sForests[treeId];
	if (!sforest) return 0;
	int_t nnodes = sforest->nNodes ;
	int_t *nodeList = sforest->nodeList ;

	gridinfo_t * grid = &(grid3d->grid2d);
	int_t myGrid = grid3d->zscp.Iam;
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* ilsum = Llu->ilsum;
	int_t* xsup = Glu_persist->xsup;
	int_t iam = grid->iam;
	int_t myrow = MYROW( iam, grid );
	int_t mycol = MYCOL( iam, grid );


	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (myrow == krow )
		{
			int_t lk = LBi(k, grid);
			int_t knsupc = SuperSize(k);
			if (myGrid == sender)
			{
				/* code */
				int_t ii = LSUM_BLK (lk);
				double* lsum_k = &lsum[ii];
				superlu_scope_t *scp = &grid->rscp;
				MPI_Reduce( lsum_k, recvbuf, knsupc * nrhs,
				            MPI_DOUBLE, MPI_SUM, kcol, scp->comm);
				xtrsTimer->trsDataSendXY += knsupc * nrhs;
				xtrsTimer->trsDataRecvXY += knsupc * nrhs;
				if (mycol == kcol)
				{
					MPI_Send( recvbuf, knsupc * nrhs, MPI_DOUBLE, receiver, k,  grid3d->zscp.comm);
					xtrsTimer->trsDataSendZ += knsupc * nrhs;
				}
			}
			else
			{
				if (mycol == kcol)
				{
					MPI_Status status;
					MPI_Recv( recvbuf, knsupc * nrhs, MPI_DOUBLE, sender, k, grid3d->zscp.comm, &status );
					xtrsTimer->trsDataRecvZ += knsupc * nrhs;
					int_t ii = LSUM_BLK (lk);
					double* dest = &lsum[ii];
					double* tempv = recvbuf;
					for (int_t j = 0; j < nrhs; ++j)
					{
						for (int_t i = 0; i < knsupc; ++i)
							dest[i + j * knsupc] += tempv[i + j * knsupc];
					}
				}

			}
		}
	}

	return 0;
}


int_t zAllocBcast(int_t size, void** ptr, gridinfo3d_t* grid3d)
{
	
	if (size < 1) return 0;
	if (grid3d->zscp.Iam)
	{
		*ptr = NULL;
		*ptr = SUPERLU_MALLOC(size);
	}
	MPI_Bcast(*ptr, size, MPI_BYTE, 0, grid3d->zscp.comm);

	return 0;
}



int_t bsolve_Xt_bcast(int_t ilvl, xT_struct *xT_s, int_t nrhs, dtrf3Dpartition_t*  trf3Dpartition,
                     dLUstruct_t * LUstruct,gridinfo3d_t* grid3d , xtrsTimer_t *xtrsTimer)
{
	sForest_t** sForests = trf3Dpartition->sForests;
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t *ilsum = Llu->ilsum;
    int_t* xsup = Glu_persist->xsup;

	int_t maxLvl = log2i(grid3d->zscp.Np) + 1;
	gridinfo_t * grid = &(grid3d->grid2d);
	int_t myGrid = grid3d->zscp.Iam;
		int_t iam = grid->iam;
	int_t myrow = MYROW( iam, grid );
	int_t mycol = MYCOL( iam, grid );


	double *xT = xT_s->xT;
	int_t *ilsumT = xT_s->ilsumT;
	int_t ldaspaT = xT_s->ldaspaT;


	int_t sender, receiver;

	if ((myGrid % (1 << (ilvl + 1))) == 0)
	{
		receiver = myGrid + (1 << ilvl);
		sender = myGrid;
	}
	else
	{
		receiver = myGrid;
		sender = myGrid - (1 << ilvl);
	}

	for (int_t alvl = ilvl + 1; alvl < maxLvl; ++alvl)
	{
		/* code */

		int_t treeId = trf3Dpartition->myTreeIdxs[alvl];
		sForest_t* sforest = trf3Dpartition->sForests[treeId];
		if (sforest)
		{
			/* code */
			int_t nnodes = sforest->nNodes;
			int_t* nodeList = sforest->nodeList;
			for (int_t k0 = 0; k0 < nnodes ; ++k0)
			{
				/* code */
				int_t k = nodeList[k0];
				int_t krow = PROW (k, grid);
				int_t kcol = PCOL (k, grid);
				int_t knsupc = SuperSize (k);
				if (myGrid == sender)
				{
					/* code */
					if (mycol == kcol &&   myrow == krow)
					{

						int_t lk = LBj (k, grid);
						int_t ii = XT_BLK (lk);
						double* xk = &xT[ii];
						MPI_Send( xk, knsupc * nrhs, MPI_DOUBLE, receiver, k, 
						           grid3d->zscp.comm);
						xtrsTimer->trsDataSendZ += knsupc * nrhs;

					}
				}
				else
				{
					if (mycol == kcol)
					{
						/* code */
						if (myrow == krow )
						{
							/* code */
							int_t lk = LBj (k, grid);
							int_t ii = XT_BLK (lk);
							double* xk = &xT[ii];
							MPI_Status status;
							MPI_Recv( xk, knsupc * nrhs, MPI_DOUBLE, sender,k,
							           grid3d->zscp.comm, &status);
							xtrsTimer->trsDataRecvZ += knsupc * nrhs;
						}
						bCastXk2Pck( k,  xT_s,  nrhs, LUstruct, grid, xtrsTimer);
					}

				}

			}
		}
	}


	return 0;
}




int_t lsumForestFsolve(int_t k,
                       double *lsum, double *x, double* rtemp,  xT_struct *xT_s, int    nrhs,
                       dLUstruct_t * LUstruct,
                       dtrf3Dpartition_t*  trf3Dpartition,
                       gridinfo3d_t* grid3d, SuperLUStat_t * stat)
{
	gridinfo_t * grid = &(grid3d->grid2d);
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* xsup = Glu_persist->xsup;

	int_t iam = grid->iam;
	int_t myrow = MYROW( iam, grid );
	int_t lk = LBj( k, grid ); /* Local block number, column-wise. */
	int_t *lsub = Llu->Lrowind_bc_ptr[lk];
	if (!lsub) return 0;
	double* lusup = Llu->Lnzval_bc_ptr[lk];
	int nsupr = lsub[1];
	int_t nlb = lsub[0];
	int_t lptr = BC_HEADER;
	int_t luptr = 0;
	int_t krow = PROW (k, grid);
	int knsupc = SuperSize(k);
	if (myrow == krow)
	{
		/* code */
		nlb = lsub[0] - 1;
		lptr +=  LB_DESCRIPTOR + knsupc;
		luptr += knsupc;
	}

	double *xT = xT_s->xT;
	int_t *ilsumT = xT_s->ilsumT;
	int_t ldaspaT = xT_s->ldaspaT;


	int_t *ilsum = Llu->ilsum;
	int_t ii = XT_BLK (lk);
	double* xk = &xT[ii];
	for (int_t lb = 0; lb < nlb; ++lb)
	{
		int_t ik = lsub[lptr]; /* Global block number, row-wise. */
		int nbrow = lsub[lptr + 1];
		double alpha = 1.0, beta = 0.0;
#ifdef _CRAY
		SGEMM( ftcs2, ftcs2, &nbrow, &nrhs, &knsupc,
		       &alpha, &lusup[luptr], &nsupr, xk,
		       &knsupc, &beta, rtemp, &nbrow );
#elif defined (USE_VENDOR_BLAS)
		dgemm_( "N", "N", &nbrow, &nrhs, &knsupc,
		        &alpha, &lusup[luptr], &nsupr, xk,
		        &knsupc, &beta, rtemp, &nbrow, 1, 1 );
#else
		dgemm_( "N", "N", &nbrow, &nrhs, &knsupc,
		        &alpha, &lusup[luptr], &nsupr, xk,
		        &knsupc, &beta, rtemp, &nbrow );
#endif
		stat->ops[SOLVE] += 2 * nbrow * nrhs * knsupc + nbrow * nrhs;

		int_t lk = LBi( ik, grid ); /* Local block number, row-wise. */
		int_t iknsupc = SuperSize( ik );
		int_t il = LSUM_BLK( lk );
		double* dest = &lsum[il];
		lptr += LB_DESCRIPTOR;
		int_t rel = xsup[ik]; /* Global row index of block ik. */
		for (int_t i = 0; i < nbrow; ++i)
		{
			int_t irow = lsub[lptr++] - rel; /* Relative row. */
			for (int_t j = 0; j < nrhs; ++j)
				dest[irow + j * iknsupc] -= rtemp[i + j * nbrow];
		}
		luptr += nbrow;
	}

	return 0;
}



int_t nonLeafForestForwardSolve3d( int_t treeId,  dLUstruct_t * LUstruct,
                                   dScalePermstruct_t * ScalePermstruct,
                                   dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                                   double * x, double * lsum,
                                   xT_struct *xT_s,
                                   double * recvbuf, double* rtemp,
                                   MPI_Request * send_req,
                                   int nrhs,
                                   dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{

	sForest_t** sForests = trf3Dpartition->sForests;

	sForest_t* sforest = sForests[treeId];
	if (!sforest)
	{
		/* code */
		return 0;
	}
	int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
	if (nnodes < 1) return 1;
	int_t *perm_c_supno = sforest->nodeList ;
	gridinfo_t * grid = &(grid3d->grid2d);

	dLocalLU_t *Llu = LUstruct->Llu;
	int_t *ilsum = Llu->ilsum;

	int_t* xsup =  LUstruct->Glu_persist->xsup;

	double *xT = xT_s->xT;
	int_t *ilsumT = xT_s->ilsumT;
	int_t ldaspaT = xT_s->ldaspaT;

	int_t iam = grid->iam;
	int_t myrow = MYROW (iam, grid);
	int_t mycol = MYCOL (iam, grid);

	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = perm_c_supno[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);
		// printf("doing %d \n", k);
		/**
		 * Pkk(Yk) = sumOver_PrK (Yk)
		 */
		if (myrow == krow )
		{
			double tx = SuperLU_timer_();
			lsumReducePrK(k, x, lsum, recvbuf, nrhs, LUstruct, grid,xtrsTimer);
			// xtrsTimer->trsDataRecvXY  += SuperSize (k)*nrhs + XK_H;
			xtrsTimer->tfs_comm += SuperLU_timer_() - tx;
		}

		if (mycol == kcol )
		{
			int_t lk = LBi (k, grid); /* Local block number, row-wise. */
			int_t ii = X_BLK (lk);
			if (myrow == krow )
			{
				/* Diagonal process. */
				double tx = SuperLU_timer_();
				localSolveXkYk(  LOWER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
				int_t lkj = LBj (k, grid);
				int_t jj = XT_BLK (lkj);
				int_t knsupc = SuperSize(k);
				memcpy(&xT[jj], &x[ii], knsupc * nrhs * sizeof(double) );
				xtrsTimer->tfs_compute += SuperLU_timer_() - tx;
			}                       /* if diagonal process ... */
			/*
			 * Send Xk to process column Pc[k].
			 */
			double tx = SuperLU_timer_();
			bCastXk2Pck( k,  xT_s,  nrhs, LUstruct, grid, xtrsTimer);
			xtrsTimer->tfs_comm += SuperLU_timer_() - tx;
			
			/*
			 * Perform local block modifications: lsum[i] -= U_i,k * X[k]
			 * where i is in current sforest
			 */
			tx = SuperLU_timer_();
			lsumForestFsolve(k, lsum, x, rtemp, xT_s, nrhs,
			                 LUstruct, trf3Dpartition, grid3d, stat);
			xtrsTimer->tfs_compute += SuperLU_timer_() - tx;
		}
	}                           /* for k ... */
	return 0;
}


int_t leafForestForwardSolve3d(superlu_dist_options_t *options, int_t treeId, int_t n,  dLUstruct_t * LUstruct,
                               dScalePermstruct_t * ScalePermstruct,
                               dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                               double * x, double * lsum, double * recvbuf, double* rtemp,
                               MPI_Request * send_req,
                               int nrhs,
                               dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{
	sForest_t** sForests = trf3Dpartition->sForests;

	sForest_t* sforest = sForests[treeId];
	if (!sforest) return 0;
	int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
	if (nnodes < 1)
	{
		return 1;
	}
	gridinfo_t * grid = &(grid3d->grid2d);
	int_t iam = grid->iam;
	int_t myrow = MYROW (iam, grid);
	int_t mycol = MYCOL (iam, grid);
	Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* xsup = Glu_persist->xsup;
	int_t** Lrowind_bc_ptr = Llu->Lrowind_bc_ptr;
	int_t nsupers = Glu_persist->supno[n - 1] + 1;
	int_t Pr = grid->nprow;
	int_t nlb = CEILING (nsupers, Pr);

	treeTopoInfo_t* treeTopoInfo = &sforest->topoInfo;
	int_t* eTreeTopLims = treeTopoInfo->eTreeTopLims;
	int_t *nodeList = sforest->nodeList ;


	int_t knsupc = sp_ienv_dist (3,options);
	int_t maxrecvsz = knsupc * nrhs + SUPERLU_MAX (XK_H, LSUM_H);

	int_t **fsendx_plist = Llu->fsendx_plist;
	int_t* ilsum = Llu->ilsum;

	int_t* fmod = getfmodLeaf(nlb, LUstruct);
	int_t* frecv = getfrecvLeaf(sforest, nlb, fmod, LUstruct, grid);
	Llu->frecv = frecv;
	int_t  nfrecvx = getNfrecvxLeaf(sforest, LUstruct, grid);
	int_t nleaf = 0;
	int_t nfrecvmod = getNfrecvmodLeaf(&nleaf, sforest, frecv, fmod,  grid);


	/* factor the leaf to being the factorization*/
	for (int_t k0 = 0; k0 < nnodes && nleaf; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);
		if (myrow == krow && mycol == kcol)
		{
			/* Diagonal process */
			int_t knsupc = SuperSize (k);
			int_t lk = LBi (k, grid);
			if (frecv[lk] == 0 && fmod[lk] == 0)
			{
				double tx = SuperLU_timer_();
				fmod[lk] = -1;  /* Do not solve X[k] in the future. */
				int_t ii = X_BLK (lk);

				int_t lkj = LBj (k, grid); /* Local block number, column-wise. */
				int_t* lsub = Lrowind_bc_ptr[lkj];
				localSolveXkYk(  LOWER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
				iBcastXk2Pck( k,  &x[ii - XK_H],  nrhs, fsendx_plist, send_req, LUstruct, grid,xtrsTimer);
				nleaf--;
				/*
				 * Perform local block modifications: lsum[i] -= L_i,k * X[k]
				 */
				int_t nb = lsub[0] - 1;
				int_t lptr = BC_HEADER + LB_DESCRIPTOR + knsupc;
				int_t luptr = knsupc; /* Skip diagonal block L(k,k). */

				dlsum_fmod_leaf (treeId, trf3Dpartition, lsum, x, &x[ii], rtemp, nrhs, knsupc, k,
				                 fmod, nb, lptr, luptr, xsup, grid, Llu,
				                 send_req, stat, xtrsTimer);
				xtrsTimer->tfs_compute += SuperLU_timer_() - tx;
			}
		}                       /* if diagonal process ... */
	}


	while (nfrecvx || nfrecvmod)
	{
		/* While not finished. */
		/* Receive a message. */
		MPI_Status status;
		double tx = SuperLU_timer_();
		MPI_Recv (recvbuf, maxrecvsz, MPI_DOUBLE,
		          MPI_ANY_SOURCE, MPI_ANY_TAG, grid->comm, &status);
		xtrsTimer->tfs_comm += SuperLU_timer_() - tx;
		int_t k = *recvbuf;
		xtrsTimer->trsDataRecvXY  += SuperSize (k)*nrhs + XK_H;
		tx = SuperLU_timer_();
		switch (status.MPI_TAG)
		{
		case Xk:
		{
			--nfrecvx;
			int_t lk = LBj (k, grid); /* Local block number, column-wise. */
			int_t *lsub = Lrowind_bc_ptr[lk];

			if (lsub)
			{
				int_t nb = lsub[0];
				int_t lptr = BC_HEADER;
				int_t luptr = 0;
				int_t knsupc = SuperSize (k);

				/*
				 * Perform local block modifications: lsum[i] -= L_i,k * X[k]
				 */
				dlsum_fmod_leaf (treeId, trf3Dpartition, lsum, x, &recvbuf[XK_H], rtemp, nrhs, knsupc, k,
				                 fmod, nb, lptr, luptr, xsup, grid, Llu,
				                 send_req, stat, xtrsTimer);
			}                   /* if lsub */

			break;
		}

		case LSUM:             /* Receiver must be a diagonal process */
		{
			--nfrecvmod;
			int_t lk = LBi (k, grid); /* Local block number, row-wise. */
			int_t ii = X_BLK (lk);
			int_t knsupc = SuperSize (k);
			double* tempv = &recvbuf[LSUM_H];
			for (int_t j = 0; j < nrhs; ++j)
			{
				for (int_t i = 0; i < knsupc; ++i)
					x[i + ii + j * knsupc] += tempv[i + j * knsupc];
			}

			if ((--frecv[lk]) == 0 && fmod[lk] == 0)
			{
				fmod[lk] = -1;  /* Do not solve X[k] in the future. */
				lk = LBj (k, grid); /* Local block number, column-wise. */
				int_t *lsub = Lrowind_bc_ptr[lk];
				localSolveXkYk(  LOWER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
				/*
				  * Send Xk to process column Pc[k].
				  */
				iBcastXk2Pck( k,  &x[ii - XK_H],  nrhs, fsendx_plist, send_req, LUstruct, grid, xtrsTimer);
				/*
				 * Perform local block modifications.
				 */
				int_t nb = lsub[0] - 1;
				int_t lptr = BC_HEADER + LB_DESCRIPTOR + knsupc;
				int_t luptr = knsupc; /* Skip diagonal block L(k,k). */

				dlsum_fmod_leaf (treeId, trf3Dpartition, lsum, x, &x[ii], rtemp, nrhs, knsupc, k,
				                 fmod, nb, lptr, luptr, xsup, grid, Llu,
				                 send_req, stat, xtrsTimer);
			}                   /* if */

			break;
		}

		default:
		{
			// printf ("(%2d) Recv'd wrong message tag %4d\n", status.MPI_TAG);
			break;
		}

		}                       /* switch */
		xtrsTimer->tfs_compute += SuperLU_timer_() - tx;
	}                           /* while not finished ... */
	SUPERLU_FREE (fmod);
	SUPERLU_FREE (frecv);
	double tx = SuperLU_timer_();
	for (int_t i = 0; i < Llu->SolveMsgSent; ++i)
	{
		MPI_Status status;
		MPI_Wait (&send_req[i], &status);
	}
	Llu->SolveMsgSent = 0;
	xtrsTimer->tfs_comm += SuperLU_timer_() - tx;
	MPI_Barrier (grid->comm);
	return 0;
}



int_t* getfmodLeaf(int_t nlb, dLUstruct_t * LUstruct)
{
	int_t* fmod;
	dLocalLU_t *Llu = LUstruct->Llu;
	if (!(fmod = intMalloc_dist (nlb)))
		ABORT ("Calloc fails for fmod[].");
	for (int_t i = 0; i < nlb; ++i)
		fmod[i] = Llu->fmod[i];

	return fmod;
}

int_t* getfrecvLeaf( sForest_t* sforest, int_t nlb, int_t* fmod,
                     dLUstruct_t * LUstruct, gridinfo_t * grid)
{

	dLocalLU_t *Llu = LUstruct->Llu;
	int_t* frecv;
	if (!(frecv = intMalloc_dist (nlb)))
		ABORT ("Malloc fails for frecv[].");
	int_t *mod_bit = Llu->mod_bit;
	superlu_scope_t *scp = &grid->rscp;
	for (int_t k = 0; k < nlb; ++k)
		mod_bit[k] = 0;
	int_t iam = grid->iam;
	int_t myrow = MYROW (iam, grid);
	int_t mycol = MYCOL (iam, grid);

	int_t nnodes =   sforest->nNodes ;
	int_t *nodeList = sforest->nodeList ;
	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (myrow == krow)
		{
			int_t lk = LBi (k, grid); /* local block number */
			int_t kcol = PCOL (k, grid);
			if (mycol != kcol && fmod[lk])
				mod_bit[lk] = 1;    /* contribution from off-diagonal */
		}
	}
	/* Every process receives the count, but it is only useful on the
	   diagonal processes.  */
	MPI_Allreduce (mod_bit, frecv, nlb, mpi_int_t, MPI_SUM, scp->comm);


	return frecv;
}

int_t getNfrecvmodLeaf(int_t* nleaf, sForest_t* sforest, int_t* frecv, int_t* fmod, gridinfo_t * grid)
{
	int_t iam = grid->iam;
	int_t myrow = MYROW (iam, grid);
	int_t mycol = MYCOL (iam, grid);

	int_t nnodes =   sforest->nNodes ;
	int_t *nodeList = sforest->nodeList ;
	int_t nfrecvmod = 0;
	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (myrow == krow)
		{
			int_t lk = LBi (k, grid); /* local block number */
			int_t kcol = PCOL (k, grid);
			if (mycol == kcol)
			{
				/* diagonal process */
				nfrecvmod += frecv[lk];
				if (!frecv[lk] && !fmod[lk])
					++(*nleaf);
			}
		}
	}
	return nfrecvmod;
}

int_t getNfrecvxLeaf(sForest_t* sforest, dLUstruct_t * LUstruct, gridinfo_t * grid)
{
	int_t iam = grid->iam;
	int_t myrow = MYROW (iam, grid);
	int_t mycol = MYCOL (iam, grid);

	int_t** Lrowind_bc_ptr = LUstruct->Llu->Lrowind_bc_ptr;
	int_t nnodes =   sforest->nNodes ;
	int_t *nodeList = sforest->nodeList ;
	int_t nfrecvx = 0;
	for (int_t k0 = 0; k0 < nnodes; ++k0)
	{
		int_t k = nodeList[k0];
		int_t krow = PROW (k, grid);
		int_t kcol = PCOL (k, grid);

		if (mycol == kcol && myrow != krow)
		{
			int_t lk = LBj(k, grid);
			int_t* lsub = Lrowind_bc_ptr[lk];
			if (lsub)
			{
				if (lsub[0] > 0)
				{
					/* code */
					nfrecvx++;
				}
			}

		}
	}

	return nfrecvx;
}


void dlsum_fmod_leaf (
    int_t treeId,
    dtrf3Dpartition_t*  trf3Dpartition,
    double *lsum,    /* Sum of local modifications.                        */
    double *x,       /* X array (local)                                    */
    double *xk,      /* X[k].                                              */
    double *rtemp,   /* Result of full matrix-vector multiply.             */
    int   nrhs,      /* Number of right-hand sides.                        */
    int   knsupc,    /* Size of supernode k.                               */
    int_t k,         /* The k-th component of X.                           */
    int_t *fmod,     /* Modification count for L-solve.                    */
    int_t nlb,       /* Number of L blocks.                                */
    int_t lptr,      /* Starting position in lsub[*].                      */
    int_t luptr,     /* Starting position in lusup[*].                     */
    int_t *xsup,
    gridinfo_t *grid,
    dLocalLU_t *Llu,
    MPI_Request send_req[], /* input/output */
    SuperLUStat_t *stat,xtrsTimer_t *xtrsTimer)

{
	double alpha = 1.0, beta = 0.0;
	double *lusup, *lusup1;
	double *dest;
	int    iam, iknsupc, myrow, nbrow, nsupr, nsupr1, p, pi;
	int_t  i, ii, ik, il, ikcol, irow, j, lb, lk, lib, rel;
	int_t  *lsub, *lsub1, nlb1, lptr1, luptr1;
	int_t  *ilsum = Llu->ilsum; /* Starting position of each supernode in lsum.   */
	int_t  *frecv = Llu->frecv;
	int_t  **fsendx_plist = Llu->fsendx_plist;
	MPI_Status status;
	int test_flag;

#if ( PROFlevel>=1 )
	double t1, t2;
	float msg_vol = 0, msg_cnt = 0;
#endif
#if ( PROFlevel>=1 )
	TIC(t1);
#endif

	iam = grid->iam;
	myrow = MYROW( iam, grid );
	lk = LBj( k, grid ); /* Local block number, column-wise. */
	lsub = Llu->Lrowind_bc_ptr[lk];
	lusup = Llu->Lnzval_bc_ptr[lk];
	nsupr = lsub[1];

	for (lb = 0; lb < nlb; ++lb)
	{
		ik = lsub[lptr]; /* Global block number, row-wise. */
		nbrow = lsub[lptr + 1];
#ifdef _CRAY
		SGEMM( ftcs2, ftcs2, &nbrow, &nrhs, &knsupc,
		       &alpha, &lusup[luptr], &nsupr, xk,
		       &knsupc, &beta, rtemp, &nbrow );
#elif defined (USE_VENDOR_BLAS)
		dgemm_( "N", "N", &nbrow, &nrhs, &knsupc,
		        &alpha, &lusup[luptr], &nsupr, xk,
		        &knsupc, &beta, rtemp, &nbrow, 1, 1 );
#else
		dgemm_( "N", "N", &nbrow, &nrhs, &knsupc,
		        &alpha, &lusup[luptr], &nsupr, xk,
		        &knsupc, &beta, rtemp, &nbrow );
#endif
		stat->ops[SOLVE] += 2 * nbrow * nrhs * knsupc + nbrow * nrhs;

		lk = LBi( ik, grid ); /* Local block number, row-wise. */
		iknsupc = SuperSize( ik );
		il = LSUM_BLK( lk );
		dest = &lsum[il];
		lptr += LB_DESCRIPTOR;
		rel = xsup[ik]; /* Global row index of block ik. */
		for (i = 0; i < nbrow; ++i)
		{
			irow = lsub[lptr++] - rel; /* Relative row. */
			RHS_ITERATE(j)
			dest[irow + j * iknsupc] -= rtemp[i + j * nbrow];
		}
		luptr += nbrow;

#if ( PROFlevel>=1 )
		TOC(t2, t1);
		stat->utime[SOL_GEMM] += t2;
#endif


		if ( (--fmod[lk]) == 0  )   /* Local accumulation done. */
		{
			if (trf3Dpartition->supernode2treeMap[ik] == treeId)
			{
				ikcol = PCOL( ik, grid );
				p = PNUM( myrow, ikcol, grid );
				if ( iam != p )
				{
#ifdef ISEND_IRECV
					MPI_Isend( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
					           MPI_DOUBLE, p, LSUM, grid->comm,
					           &send_req[Llu->SolveMsgSent++] );
#else
#ifdef BSEND
					MPI_Bsend( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
					           MPI_DOUBLE, p, LSUM, grid->comm );
#else
					MPI_Send( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
					          MPI_DOUBLE, p, LSUM, grid->comm );
#endif
#endif
					xtrsTimer->trsDataSendXY += iknsupc * nrhs + LSUM_H;
				}
				else     /* Diagonal process: X[i] += lsum[i]. */
				{
					ii = X_BLK( lk );
					RHS_ITERATE(j)
					for (i = 0; i < iknsupc; ++i)
						x[i + ii + j * iknsupc] += lsum[i + il + j * iknsupc];
					if ( frecv[lk] == 0 )   /* Becomes a leaf node. */
					{
						fmod[lk] = -1; /* Do not solve X[k] in the future. */


						lk = LBj( ik, grid );/* Local block number, column-wise. */
						lsub1 = Llu->Lrowind_bc_ptr[lk];
						lusup1 = Llu->Lnzval_bc_ptr[lk];
						nsupr1 = lsub1[1];
#ifdef _CRAY
						STRSM(ftcs1, ftcs1, ftcs2, ftcs3, &iknsupc, &nrhs, &alpha,
						      lusup1, &nsupr1, &x[ii], &iknsupc);
#elif defined (USE_VENDOR_BLAS)
						dtrsm_("L", "L", "N", "U", &iknsupc, &nrhs, &alpha,
						       lusup1, &nsupr1, &x[ii], &iknsupc, 1, 1, 1, 1);
#else
						dtrsm_("L", "L", "N", "U", &iknsupc, &nrhs, &alpha,
						       lusup1, &nsupr1, &x[ii], &iknsupc);
#endif


						stat->ops[SOLVE] += iknsupc * (iknsupc - 1) * nrhs;

						/*
						 * Send Xk to process column Pc[k].
						 */
						for (p = 0; p < grid->nprow; ++p)
						{
							if ( fsendx_plist[lk][p] != EMPTY )
							{
								pi = PNUM( p, ikcol, grid );
#ifdef ISEND_IRECV
								MPI_Isend( &x[ii - XK_H], iknsupc * nrhs + XK_H,
								           MPI_DOUBLE, pi, Xk, grid->comm,
								           &send_req[Llu->SolveMsgSent++] );
#else
#ifdef BSEND
								MPI_Bsend( &x[ii - XK_H], iknsupc * nrhs + XK_H,
								           MPI_DOUBLE, pi, Xk, grid->comm );
#else
								MPI_Send( &x[ii - XK_H], iknsupc * nrhs + XK_H,
								          MPI_DOUBLE, pi, Xk, grid->comm );
#endif
#endif

							}
						}
						xtrsTimer->trsDataSendXY += iknsupc * nrhs + XK_H;
						/*
						 * Perform local block modifications.
						 */
						nlb1 = lsub1[0] - 1;
						lptr1 = BC_HEADER + LB_DESCRIPTOR + iknsupc;
						luptr1 = iknsupc; /* Skip diagonal block L(I,I). */

						dlsum_fmod_leaf(treeId, trf3Dpartition,
						                lsum, x, &x[ii], rtemp, nrhs, iknsupc, ik,
						                fmod, nlb1, lptr1, luptr1, xsup,
						                grid, Llu, send_req, stat,xtrsTimer);
					} /* if frecv[lk] == 0 */
				} /* if iam == p */
			}
		}/* if fmod[lk] == 0 */

	} /* for lb ... */

} /* dLSUM_FMOD_LEAF */




int_t dlasum_bmod_Tree(int_t  pTree, int_t cTree, double *lsum, double *x,
                       xT_struct *xT_s,
                       int    nrhs, lsumBmod_buff_t* lbmod_buf,
                       dLUstruct_t * LUstruct,
                       dtrf3Dpartition_t*  trf3Dpartition,
                       gridinfo3d_t* grid3d, SuperLUStat_t * stat)
{
    gridinfo_t * grid = &(grid3d->grid2d);
    sForest_t* pforest = trf3Dpartition->sForests[pTree];
    sForest_t* cforest = trf3Dpartition->sForests[cTree];
    if (!pforest || !cforest) return 0;

    int_t nnodes = pforest->nNodes;
    if (nnodes < 1) return 0;
    int_t* nodeList =  pforest->nodeList;
    int_t iam = grid->iam;
    int_t mycol = MYCOL( iam, grid );
    for (int_t k0 = 0; k0 < nnodes; ++k0)
    {
        /* code */
        int_t k = nodeList[k0];
        int_t kcol = PCOL (k, grid);
        if (mycol == kcol)
        {
            /* code */
            lsumForestBsolve(k, cTree, lsum, x, xT_s, nrhs, lbmod_buf,
                             LUstruct, trf3Dpartition, grid3d, stat);
        }
    }
    return 0;
}


int_t initLsumBmod_buff(int_t ns, int_t nrhs, lsumBmod_buff_t* lbmod_buf)
{
    lbmod_buf->tX = SUPERLU_MALLOC(ns * nrhs * sizeof(double));
    lbmod_buf->tU = SUPERLU_MALLOC(ns * ns * sizeof(double));
    lbmod_buf->indCols = SUPERLU_MALLOC(ns * sizeof(int_t));
    return 0;
}

int_t freeLsumBmod_buff(lsumBmod_buff_t* lbmod_buf)
{
    SUPERLU_FREE(lbmod_buf->tX);
    SUPERLU_FREE(lbmod_buf->tU);
    SUPERLU_FREE(lbmod_buf->indCols);
    return 0;
}

int_t getldu(int_t knsupc, int_t iklrow, int_t* usub )
{
    int_t ldu = 0;

    for (int_t jj = 0; jj < knsupc; ++jj)
    {
        int_t fnz = usub[jj];
        if ( fnz < iklrow )
        {
            int_t segsize = iklrow - fnz;
            ldu = SUPERLU_MAX(ldu, segsize);
        }

    }
    return ldu;
}

int_t packUblock(int_t ldu, int_t* indCols,
                 int_t knsupc, int_t iklrow,  int_t* usub,
                 double* tempu, double* uval )
{
    int_t ncols = 0;
    for (int_t jj = 0; jj < knsupc; ++jj)
    {

        int_t segsize = iklrow - usub[jj];
        if ( segsize )
        {
            int_t lead_zero = ldu - segsize;
            for (int_t i = 0; i < lead_zero; ++i) tempu[i] = 0.0;
            tempu += lead_zero;
            for (int_t i = 0; i < segsize; ++i)
            {
                tempu[i] = uval[i];
            }

            uval += segsize;
            tempu += segsize;
            indCols[ncols] = jj;
            ncols++;
        }

    } /* for jj ... */

    return ncols;
}


int_t packXbmod( int_t knsupc, int_t ncols, int_t nrhs, int_t* indCols, double* xk, double* tempx)
{

    for (int_t j = 0; j < nrhs; ++j)
    {
        double* dest = &tempx[j * ncols];
        double* y = &xk[j * knsupc];

        for (int_t jj = 0; jj < ncols; ++jj)
        {
            dest[jj] = y[indCols[jj]];
        } /* for jj ... */
    }

    return 0;
}

int_t lsumBmod(int_t gik, int_t gjk, int_t nrhs, lsumBmod_buff_t* lbmod_buf,
               int_t* usub,  double* uval,
               double* xk, double* lsum, int_t* xsup, SuperLUStat_t * stat)
{

    int_t* indCols = lbmod_buf->indCols;
    double* tempu = lbmod_buf->tU;
    double* tempx = lbmod_buf->tX;
    int_t iknsupc = SuperSize( gik );
    int_t knsupc = SuperSize( gjk );
    int_t iklrow = FstBlockC( gik + 1 );
    int_t ldu = getldu(knsupc, iklrow,
                       usub // use &usub[i]
                      );

    int_t ncols = packUblock(ldu, indCols, knsupc, iklrow, usub,
                             tempu, uval );

    double alpha = -1.0;
    double beta = 1;

    double* X;

    if (ncols < knsupc)
    {
        /* code */
        packXbmod(knsupc, ncols, nrhs, indCols, xk, tempx);
        X = tempx;
    }
    else
    {
        X = xk;
    }

    double* V = &lsum[iknsupc - ldu];


#if defined (USE_VENDOR_BLAS)
	dgemm_("N", "N", &ldu, &nrhs, &ncols, &alpha,
	tempu, &ldu,
	X, &ncols, &beta, V, &iknsupc, 1, 1);
#else
	dgemm_("N", "N", &ldu, &nrhs, &ncols, &alpha,
	tempu, &ldu,
	X, &ncols, &beta, V, &iknsupc);
#endif





    stat->ops[SOLVE] += 2 * ldu * nrhs * ncols;
    return 0;
}

int_t lsumForestBsolve(int_t k, int_t treeId,
                       double *lsum, double *x,  xT_struct *xT_s, int    nrhs, lsumBmod_buff_t* lbmod_buf,
                       dLUstruct_t * LUstruct,
                       dtrf3Dpartition_t*  trf3Dpartition,
                       gridinfo3d_t* grid3d, SuperLUStat_t * stat)
{
    gridinfo_t * grid = &(grid3d->grid2d);
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t* xsup = Glu_persist->xsup;
    int_t  *Urbs = Llu->Urbs; /* Number of row blocks in each block column of U. */
    Ucb_indptr_t **Ucb_indptr = Llu->Ucb_indptr;/* Vertical linked list pointing to Uindex[] */
    int_t  **Ucb_valptr = Llu->Ucb_valptr;      /* Vertical linked list pointing to Unzval[] */
    int_t iam = grid->iam;
    int_t myrow = MYROW( iam, grid );
    int_t knsupc = SuperSize( k );
    double *xT = xT_s->xT;
    int_t *ilsumT = xT_s->ilsumT;
    int_t ldaspaT = xT_s->ldaspaT;

    int_t lk = LBj( k, grid ); /* Local block number, column-wise. */
    int_t nub = Urbs[lk];      /* Number of U blocks in block column lk */
    int_t *ilsum = Llu->ilsum;
    int_t ii = XT_BLK (lk);
    double* xk = &xT[ii];
    for (int_t ub = 0; ub < nub; ++ub)
    {
        int_t ik = Ucb_indptr[lk][ub].lbnum; /* Local block number, row-wise. */
        int_t gik = ik * grid->nprow + myrow;/* Global block number, row-wise. */

        if (trf3Dpartition->supernode2treeMap[gik] == treeId)
        {
            int_t* usub = Llu->Ufstnz_br_ptr[ik];
            double* uval = Llu->Unzval_br_ptr[ik];
            int_t i = Ucb_indptr[lk][ub].indpos; /* Start of the block in usub[]. */
            i += UB_DESCRIPTOR;
            int_t il = LSUM_BLK( ik );
#if 1
            lsumBmod(gik, k, nrhs, lbmod_buf,
                     &usub[i], &uval[Ucb_valptr[lk][ub]], xk,
                     &lsum[il], xsup, stat);
#else
            int_t iknsupc = SuperSize( gik );
            int_t ikfrow = FstBlockC( gik );
            int_t iklrow = FstBlockC( gik + 1 );

            for (int_t j = 0; j < nrhs; ++j)
            {
                double* dest = &lsum[il + j * iknsupc];
                double* y = &xk[j * knsupc];
                int_t uptr = Ucb_valptr[lk][ub]; /* Start of the block in uval[]. */
                for (int_t jj = 0; jj < knsupc; ++jj)
                {
                    int_t fnz = usub[i + jj];
                    if ( fnz < iklrow )
                    {
                        /* Nonzero segment. */
                        /* AXPY */
                        for (int_t irow = fnz; irow < iklrow; ++irow)
                            dest[irow - ikfrow] -= uval[uptr++] * y[jj];
                        stat->ops[SOLVE] += 2 * (iklrow - fnz);
                    }
                } /* for jj ... */
            } /*for (int_t j = 0;*/
#endif

        }

    }
    return 0;
}


int_t  bCastXk2Pck  (int_t k, xT_struct *xT_s, int_t nrhs,
                     dLUstruct_t * LUstruct, gridinfo_t * grid, xtrsTimer_t *xtrsTimer)
{
    /*
     * Send Xk to process column Pc[k].
     */

    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t *ilsum = Llu->ilsum;
    int_t* xsup = Glu_persist->xsup;

    double *xT = xT_s->xT;
    int_t *ilsumT = xT_s->ilsumT;
    int_t ldaspaT = xT_s->ldaspaT;

    int_t lk = LBj (k, grid);
    int_t ii = XT_BLK (lk);
    double* xk = &xT[ii];
    superlu_scope_t *scp = &grid->cscp;
    int_t knsupc = SuperSize (k);
    int_t krow = PROW (k, grid);
    MPI_Bcast( xk, knsupc * nrhs, MPI_DOUBLE, krow,
               scp->comm);

    xtrsTimer->trsDataRecvXY  += knsupc * nrhs;
    xtrsTimer->trsDataSendXY  += knsupc * nrhs;
    return 0;
}

int_t  lsumReducePrK (int_t k, double*x, double* lsum, double* recvbuf, int_t nrhs,
                      dLUstruct_t * LUstruct, gridinfo_t * grid, xtrsTimer_t *xtrsTimer)
{
    /*
     * Send Xk to process column Pc[k].
     */

    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t *ilsum = Llu->ilsum;
    int_t* xsup = Glu_persist->xsup;

    int_t knsupc = SuperSize (k);
    int_t lk = LBi (k, grid);
    int_t iam = grid->iam;
    int_t mycol = MYCOL (iam, grid);
    int_t kcol = PCOL (k, grid);

    int_t ii = LSUM_BLK (lk);
    double* lsum_k = &lsum[ii];
    superlu_scope_t *scp = &grid->rscp;
    MPI_Reduce( lsum_k, recvbuf, knsupc * nrhs,
                MPI_DOUBLE, MPI_SUM, kcol, scp->comm);

    xtrsTimer->trsDataRecvXY  += knsupc * nrhs;
    xtrsTimer->trsDataSendXY  += knsupc * nrhs;

    if (mycol == kcol)
    {
        int_t ii = X_BLK( lk );
        double* dest = &x[ii];
        double* tempv = recvbuf;
        for (int_t j = 0; j < nrhs; ++j)
        {
            for (int_t i = 0; i < knsupc; ++i)
                x[i + ii + j * knsupc] += tempv[i + j * knsupc];
        }
    }

    return 0;
}

int_t nonLeafForestBackSolve3d( int_t treeId,  dLUstruct_t * LUstruct,
                                dScalePermstruct_t * ScalePermstruct,
                                dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                                double * x, double * lsum,
                                xT_struct *xT_s,
                                double * recvbuf,
                                MPI_Request * send_req,
                                int nrhs, lsumBmod_buff_t* lbmod_buf,
                                dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{
    sForest_t** sForests = trf3Dpartition->sForests;

    sForest_t* sforest = sForests[treeId];
    if (!sforest)
    {
        /* code */
        return 0;
    }
    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return 1;
    int_t *perm_c_supno = sforest->nodeList ;
    gridinfo_t * grid = &(grid3d->grid2d);

    dLocalLU_t *Llu = LUstruct->Llu;
    int_t *ilsum = Llu->ilsum;

    int_t* xsup =  LUstruct->Glu_persist->xsup;

    double *xT = xT_s->xT;
    int_t *ilsumT = xT_s->ilsumT;
    int_t ldaspaT = xT_s->ldaspaT;

    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);

    for (int_t k0 = nnodes - 1; k0 >= 0; --k0)
    {
        int_t k = perm_c_supno[k0];
        int_t krow = PROW (k, grid);
        int_t kcol = PCOL (k, grid);
        // printf("doing %d \n", k);
        /**
         * Pkk(Yk) = sumOver_PrK (Yk)
         */
        if (myrow == krow )
        {
            double tx = SuperLU_timer_();
            lsumReducePrK(k, x, lsum, recvbuf, nrhs, LUstruct, grid, xtrsTimer);
            xtrsTimer->tbs_comm += SuperLU_timer_() - tx;
        }

        if (mycol == kcol )
        {
            int_t lk = LBi (k, grid); /* Local block number, row-wise. */
            int_t ii = X_BLK (lk);
            if (myrow == krow )
            {
                double tx = SuperLU_timer_();
                /* Diagonal process. */
                localSolveXkYk(  UPPER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
                int_t lkj = LBj (k, grid);
                int_t jj = XT_BLK (lkj);
                int_t knsupc = SuperSize(k);
                memcpy(&xT[jj], &x[ii], knsupc * nrhs * sizeof(double) );
                xtrsTimer->tbs_compute += SuperLU_timer_() - tx;
            }                       /* if diagonal process ... */

            /*
             * Send Xk to process column Pc[k].
             */
            double tx = SuperLU_timer_();
            bCastXk2Pck( k,  xT_s,  nrhs, LUstruct, grid,xtrsTimer);
            xtrsTimer->tbs_comm += SuperLU_timer_() - tx;
            /*
             * Perform local block modifications: lsum[i] -= U_i,k * X[k]
             * where i is in current sforest
             */
            tx = SuperLU_timer_();
            lsumForestBsolve(k, treeId, lsum, x, xT_s, nrhs, lbmod_buf,
                             LUstruct, trf3Dpartition, grid3d, stat);
            xtrsTimer->tbs_compute += SuperLU_timer_() - tx;
        }
    }                           /* for k ... */
    return 0;
}




#define ISEND_IRECV



int_t leafForestBackSolve3d(superlu_dist_options_t *options, int_t treeId, int_t n,  dLUstruct_t * LUstruct,
                            dScalePermstruct_t * ScalePermstruct,
                            dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                            double * x, double * lsum, double * recvbuf,
                            MPI_Request * send_req,
                            int nrhs, lsumBmod_buff_t* lbmod_buf,
                            dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{

    gridinfo_t * grid = &(grid3d->grid2d);
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t* xsup = Glu_persist->xsup;
    sForest_t* sforest = trf3Dpartition->sForests[treeId];
    if (!sforest) return 0;
    int_t** Lrowind_bc_ptr = Llu->Lrowind_bc_ptr;
    // double** Lnzval_bc_ptr = Llu->Lnzval_bc_ptr;
    int_t *ilsum = Llu->ilsum;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);

    int_t  *Urbs = Llu->Urbs; /* Number of row blocks in each block column of U. */
    Ucb_indptr_t **Ucb_indptr = Llu->Ucb_indptr;/* Vertical linked list pointing to Uindex[] */
    int_t  **Ucb_valptr = Llu->Ucb_valptr;      /* Vertical linked list pointing to Unzval[] */
    int_t knsupc = sp_ienv_dist (3,options);
    int_t maxrecvsz = knsupc * nrhs + SUPERLU_MAX (XK_H, LSUM_H);

    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return 1;
    int_t *perm_c_supno = sforest->nodeList ;

    int_t **bsendx_plist = Llu->bsendx_plist;
    int_t Pr = grid->nprow;
    int_t nsupers = Glu_persist->supno[n - 1] + 1;
    int_t nlb = CEILING (nsupers, Pr);
    int_t* bmod =  getBmod3d(treeId, nlb, sforest, LUstruct, trf3Dpartition, grid);
    int_t* brecv = getBrecvTree(nlb, sforest, bmod, grid);
    Llu->brecv = brecv;

    int_t nbrecvmod = 0;
    int_t nroot = getNrootUsolveTree(&nbrecvmod, sforest, brecv, bmod, grid);
    int_t nbrecvx = getNbrecvX(sforest, Urbs, grid);


    /*before starting the solve; intialize the 3d lsum*/

    for (int_t k0 = nnodes - 1; k0 >= 0 ; --k0)
    {
        int_t k = perm_c_supno[k0];
        int_t krow = PROW (k, grid);
        int_t kcol = PCOL (k, grid);
        if (myrow == krow)
        {
            /* Diagonal process. */

            int_t lk = LBi (k, grid); /* Local block number, row-wise. */
            if (bmod[lk] == 0)
            {
                /* code */
                int_t il = LSUM_BLK( lk );
                int_t knsupc = SuperSize(k);
                if (mycol != kcol)
                {
                    /* code */
                    int_t p = PNUM( myrow, kcol, grid );
                    MPI_Isend( &lsum[il - LSUM_H], knsupc * nrhs + LSUM_H,
                               MPI_DOUBLE, p, LSUM, grid->comm,
                               &send_req[Llu->SolveMsgSent++] );
                    xtrsTimer->trsDataSendXY += knsupc * nrhs + LSUM_H;
                }
                else
                {
                    int_t ii = X_BLK( lk );
                    double* dest = &x[ii];
                    for (int_t j = 0; j < nrhs; ++j)
                        for (int_t i = 0; i < knsupc; ++i)
                            dest[i + j * knsupc] += lsum[i + il + j * knsupc];

                    if (brecv[lk] == 0 )
                    {
                        double tx = SuperLU_timer_();
                        bmod[lk] = -1;  /* Do not solve X[k] in the future. */

                        int_t ii = X_BLK (lk);
                        int_t lkj = LBj (k, grid); /* Local block number, column-wise */

                        localSolveXkYk(  UPPER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
                        --nroot;
                        /*
                         * Send Xk to process column Pc[k].
                         */
                        iBcastXk2Pck( k,  &x[ii - XK_H],  nrhs, bsendx_plist, send_req, LUstruct, grid,xtrsTimer);
                        /*
                         * Perform local block modifications: lsum[i] -= U_i,k * X[k]
                         */
                        if (Urbs[lkj])
                            dlsum_bmod_GG (lsum, x, &x[ii], nrhs, lbmod_buf,  k, bmod, Urbs,
                                           Ucb_indptr, Ucb_valptr, xsup, grid, Llu,
                                           send_req, stat,xtrsTimer);
                        xtrsTimer->tbs_compute += SuperLU_timer_() - tx;
                    }                   /* if root ... */
                }
            }

        }                       /* if diagonal process ... */
    }                           /* for k ... */
    while (nbrecvx || nbrecvmod)
    {
        /* While not finished. */

        /* Receive a message. */
        MPI_Status status;
        double tx = SuperLU_timer_();
        MPI_Recv (recvbuf, maxrecvsz, MPI_DOUBLE,
                  MPI_ANY_SOURCE, MPI_ANY_TAG, grid->comm, &status);
        xtrsTimer->tbs_comm += SuperLU_timer_() - tx;
        int_t k = *recvbuf;

        tx = SuperLU_timer_();
        switch (status.MPI_TAG)
        {
        case Xk:
        {
            --nbrecvx;
            xtrsTimer->trsDataRecvXY += SuperSize(k)*nrhs + XK_H;
            /*
             * Perform local block modifications:
             *         lsum[i] -= U_i,k * X[k]
             */
            dlsum_bmod_GG (lsum, x, &recvbuf[XK_H], nrhs, lbmod_buf, k, bmod, Urbs,
                           Ucb_indptr, Ucb_valptr, xsup, grid, Llu,
                           send_req, stat,xtrsTimer);
            break;
        }
        case LSUM:             /* Receiver must be a diagonal process */
        {
            --nbrecvmod;
            xtrsTimer->trsDataRecvXY += SuperSize(k)*nrhs + LSUM_H;
            int_t lk = LBi (k, grid); /* Local block number, row-wise. */
            int_t ii = X_BLK (lk);
            int_t knsupc = SuperSize (k);
            double* tempv = &recvbuf[LSUM_H];
            for (int_t j = 0; j < nrhs; ++j)
            {
                for (int_t i = 0; i < knsupc; ++i)
                    x[i + ii + j * knsupc] += tempv[i + j * knsupc];
            }

            if ((--brecv[lk]) == 0 && bmod[lk] == 0)
            {
                bmod[lk] = -1;  /* Do not solve X[k] in the future. */
                int_t lk = LBj (k, grid); /* Local block number, column-wise. */
                // int_t* lsub = Lrowind_bc_ptr[lk];
                localSolveXkYk(  UPPER_TRI,  k,  &x[ii],  nrhs, LUstruct,   grid, stat);
                iBcastXk2Pck( k,  &x[ii - XK_H],  nrhs, bsendx_plist, send_req, LUstruct, grid,xtrsTimer);
                if (Urbs[lk])
                    dlsum_bmod_GG (lsum, x, &x[ii], nrhs, lbmod_buf, k, bmod, Urbs,
                                   Ucb_indptr, Ucb_valptr, xsup, grid, Llu,
                                   send_req, stat,xtrsTimer);
            }                   /* if becomes solvable */

            break;
        }
        }                       /* switch */
        xtrsTimer->tbs_compute += SuperLU_timer_() - tx;
    }                           /* while not finished ... */

    double tx = SuperLU_timer_();
    for (int_t i = 0; i < Llu->SolveMsgSent; ++i)
    {
        MPI_Status status;
        MPI_Wait (&send_req[i], &status);
    }
    Llu->SolveMsgSent = 0;
    xtrsTimer->tbs_comm += SuperLU_timer_() - tx;
    return 0;

}



int_t getNbrecvX(sForest_t* sforest, int_t* Urbs, gridinfo_t * grid)
{
    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return 0;
    int_t *nodeList = sforest->nodeList ;

    // int_t Pr = grid->nprow;
    // int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);
    int_t nbrecvx = 0;
    for (int_t k0 = 0; k0 < nnodes ; ++k0)
    {
        /* code */
        int_t k = nodeList[k0];
        int_t krow = PROW (k, grid);
        int_t kcol = PCOL (k, grid);
        if (mycol == kcol && myrow != krow)
        {
            /* code */
            int_t lk = LBj( k, grid ); /* Local block number, column-wise. */
            int_t nub = Urbs[lk];      /* Number of U blocks in block column lk */
            if (nub > 0)
                nbrecvx++;
        }
    }

    return nbrecvx;
}


int_t getNrootUsolveTree(int_t* nbrecvmod, sForest_t* sforest, int_t* brecv, int_t* bmod, gridinfo_t * grid)
{
    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return 0;
    int_t *nodeList = sforest->nodeList ;

    // int_t Pr = grid->nprow;
    // int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);
    int_t nroot = 0;
    for (int_t k0 = 0; k0 < nnodes ; ++k0)
    {
        /* code */
        int_t k = nodeList[k0];
        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t lk = LBi (k, grid); /* local block number */
            int_t kcol = PCOL (k, grid);  /* root process in this row scope. */
            if (mycol == kcol)
            {
                /* diagonal process */
                *nbrecvmod += brecv[lk];
                if (!brecv[lk] && !bmod[lk])
                    ++nroot;

            }
        }
    }

    return nroot;
}


int_t* getBrecvTree(int_t nlb, sForest_t* sforest,  int_t* bmod, gridinfo_t * grid)
{
    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return NULL;
    int_t *nodeList = sforest->nodeList ;

    // int_t Pr = grid->nprow;
    // int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);
    superlu_scope_t *scp = &grid->rscp;



    int_t* mod_bit = SUPERLU_MALLOC(sizeof(int_t) * nlb);
    for (int_t k = 0; k < nlb; ++k)
        mod_bit[k] = 0;

    int_t* brecv = SUPERLU_MALLOC(sizeof(int_t) * nlb);


    for (int_t k0 = 0; k0 < nnodes ; ++k0)
    {
        /* code */
        int_t k = nodeList[k0];
        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t lk = LBi (k, grid); /* local block number */
            int_t kcol = PCOL (k, grid);  /* root process in this row scope */
            if (mycol != kcol )
                mod_bit[lk] = 1;    /* Contribution from off-diagonal */
        }
    }

    /* Every process receives the count, but it is only useful on the
       diagonal processes.  */
    MPI_Allreduce (mod_bit, brecv, nlb, mpi_int_t, MPI_SUM, scp->comm);

    SUPERLU_FREE(mod_bit);
    return brecv;
}


int_t* getBmod3d(int_t treeId, int_t nlb, sForest_t* sforest, dLUstruct_t * LUstruct,
                 dtrf3Dpartition_t*  trf3Dpartition, gridinfo_t * grid)
{
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t* xsup = Glu_persist->xsup;
    int_t nnodes =   sforest->nNodes ;      // number of nodes in the tree
    if (nnodes < 1) return NULL;
    int_t *nodeList = sforest->nodeList ;

    // int_t Pr = grid->nprow;
    // int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    // int_t mycol = MYCOL (iam, grid);
    int_t **Ufstnz_br_ptr = Llu->Ufstnz_br_ptr;
    int_t* bmod = SUPERLU_MALLOC(sizeof(int_t) * nlb);

    for (int_t k = 0; k < nlb; ++k)
        bmod[k] = 0;
    for (int_t k0 = 0; k0 < nnodes ; ++k0)
    {
        /* code */

        int_t k = nodeList[k0];

        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t lk = LBi (k, grid);
            bmod[lk] = 0;
            int_t* usub = Ufstnz_br_ptr[lk];
            if (usub)
            {
                /* code */
                int_t nub = usub[0];       /* Number of blocks in the block row U(k,:) */
                int_t iukp = BR_HEADER;   /* Skip header; Pointer to index[] of U(k,:) */
                // int_t rukp = 0;           /* Pointer to nzval[] of U(k,:) */
                for (int_t ii = 0; ii < nub; ii++)
                {
                    int_t jb = usub[iukp];
                    if ( trf3Dpartition->supernode2treeMap[jb] == treeId)
                    {
                        /* code */
                        bmod[lk]++;
                    }
                    iukp += UB_DESCRIPTOR;
                    iukp += SuperSize (jb);

                }


            }
            else
            {
                bmod[lk] = 0;
            }

        }
    }
    return bmod;
}


/************************************************************************/

/************************************************************************/
void dlsum_bmod_GG (
    double *lsum,        /* Sum of local modifications.                    */
    double *x,           /* X array (local).                               */
    double *xk,          /* X[k].                                          */
    int    nrhs,          /* Number of right-hand sides.                    */
    lsumBmod_buff_t* lbmod_buf,
    int_t  k,            /* The k-th component of X.                       */
    int_t  *bmod,        /* Modification count for L-solve.                */
    int_t  *Urbs,        /* Number of row blocks in each block column of U.*/
    Ucb_indptr_t **Ucb_indptr,/* Vertical linked list pointing to Uindex[].*/
    int_t  **Ucb_valptr, /* Vertical linked list pointing to Unzval[].     */
    int_t  *xsup,
    gridinfo_t *grid,
    dLocalLU_t *Llu,
    MPI_Request send_req[], /* input/output */
    SuperLUStat_t *stat
    , xtrsTimer_t *xtrsTimer)
{
    // printf("bmodding %d\n", k);
    /*
     * Purpose
     * =======
     *   Perform local block modifications: lsum[i] -= U_i,k * X[k].
     */
    double alpha = 1.0, beta = 0.0;
    int    iam, iknsupc, knsupc, myrow, nsupr, p, pi;
    int_t  fnz, gik, gikcol, i, ii, ik, ikfrow, iklrow, il, irow,
           j, jj, lk, lk1, nub, ub, uptr;
    int_t  *usub;
    double *uval, *dest, *y;
    int_t  *lsub;
    double *lusup;
    int_t  *ilsum = Llu->ilsum; /* Starting position of each supernode in lsum.   */
    int_t  *brecv = Llu->brecv;
    int_t  **bsendx_plist = Llu->bsendx_plist;
    MPI_Status status;
    int test_flag;

    iam = grid->iam;
    myrow = MYROW( iam, grid );
    knsupc = SuperSize( k );
    lk = LBj( k, grid ); /* Local block number, column-wise. */
    nub = Urbs[lk];      /* Number of U blocks in block column lk */

    for (ub = 0; ub < nub; ++ub)
    {
        ik = Ucb_indptr[lk][ub].lbnum; /* Local block number, row-wise. */
        usub = Llu->Ufstnz_br_ptr[ik];
        uval = Llu->Unzval_br_ptr[ik];
        i = Ucb_indptr[lk][ub].indpos; /* Start of the block in usub[]. */
        i += UB_DESCRIPTOR;
        il = LSUM_BLK( ik );
        gik = ik * grid->nprow + myrow;/* Global block number, row-wise. */
        iknsupc = SuperSize( gik );
#if 1
        lsumBmod(gik, k, nrhs, lbmod_buf,
                 &usub[i], &uval[Ucb_valptr[lk][ub]], xk,
                 &lsum[il], xsup, stat);
#else
        
        ikfrow = FstBlockC( gik );
        iklrow = FstBlockC( gik + 1 );

        for (int_t j = 0; j < nrhs; ++j)
        {
            dest = &lsum[il + j * iknsupc];
            y = &xk[j * knsupc];
            uptr = Ucb_valptr[lk][ub]; /* Start of the block in uval[]. */
            for (jj = 0; jj < knsupc; ++jj)
            {
                fnz = usub[i + jj];
                if ( fnz < iklrow )   /* Nonzero segment. */
                {
                    /* AXPY */
                    for (irow = fnz; irow < iklrow; ++irow)
                        dest[irow - ikfrow] -= uval[uptr++] * y[jj];
                    stat->ops[SOLVE] += 2 * (iklrow - fnz);
                }
            } /* for jj ... */
        } /*for (int_t j = 0;*/
#endif
        // printf(" updating %d  %d  \n",ik, bmod[ik] );
        if ( (--bmod[ik]) == 0 )   /* Local accumulation done. */
        {
            // printf("Local accumulation done %d  %d, brecv[ik]=%d  ",ik, bmod[ik],brecv[ik] );
            gikcol = PCOL( gik, grid );
            p = PNUM( myrow, gikcol, grid );
            if ( iam != p )
            {
#ifdef ISEND_IRECV
                MPI_Isend( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
                           MPI_DOUBLE, p, LSUM, grid->comm,
                           &send_req[Llu->SolveMsgSent++] );
#else
#ifdef BSEND
                MPI_Bsend( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
                           MPI_DOUBLE, p, LSUM, grid->comm );
#else
                MPI_Send( &lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H,
                          MPI_DOUBLE, p, LSUM, grid->comm );
#endif
#endif
#if ( DEBUGlevel>=2 )
                printf("(%2d) Sent LSUM[%2.0f], size %2d, to P %2d\n",
                       iam, lsum[il - LSUM_H], iknsupc * nrhs + LSUM_H, p);
#endif
                xtrsTimer->trsDataSendXY += iknsupc * nrhs + LSUM_H;
            }
            else     /* Diagonal process: X[i] += lsum[i]. */
            {
                ii = X_BLK( ik );
                dest = &x[ii];
                for (int_t j = 0; j < nrhs; ++j)
                    for (i = 0; i < iknsupc; ++i)
                        dest[i + j * iknsupc] += lsum[i + il + j * iknsupc];
                if ( !brecv[ik] )   /* Becomes a leaf node. */
                {
                    bmod[ik] = -1; /* Do not solve X[k] in the future. */
                    lk1 = LBj( gik, grid ); /* Local block number. */
                    lsub = Llu->Lrowind_bc_ptr[lk1];
                    lusup = Llu->Lnzval_bc_ptr[lk1];
                    nsupr = lsub[1];
#ifdef _CRAY
                    STRSM(ftcs1, ftcs3, ftcs2, ftcs2, &iknsupc, &nrhs, &alpha,
                          lusup, &nsupr, &x[ii], &iknsupc);
#elif defined (USE_VENDOR_BLAS)
                    dtrsm_("L", "U", "N", "N", &iknsupc, &nrhs, &alpha,
                           lusup, &nsupr, &x[ii], &iknsupc, 1, 1, 1, 1);
#else
                    dtrsm_("L", "U", "N", "N", &iknsupc, &nrhs, &alpha,
                           lusup, &nsupr, &x[ii], &iknsupc);
#endif
                    stat->ops[SOLVE] += iknsupc * (iknsupc + 1) * nrhs;
#if ( DEBUGlevel>=2 )
                    printf("(%2d) Solve X[%2d]\n", iam, gik);
#endif

                    /*
                     * Send Xk to process column Pc[k].
                     */
                    for (p = 0; p < grid->nprow; ++p)
                    {
                        if ( bsendx_plist[lk1][p] != EMPTY )
                        {
                            pi = PNUM( p, gikcol, grid );
#ifdef ISEND_IRECV
                            MPI_Isend( &x[ii - XK_H], iknsupc * nrhs + XK_H,
                                       MPI_DOUBLE, pi, Xk, grid->comm,
                                       &send_req[Llu->SolveMsgSent++] );
#else
#ifdef BSEND
                            MPI_Bsend( &x[ii - XK_H], iknsupc * nrhs + XK_H,
                                       MPI_DOUBLE, pi, Xk, grid->comm );
#else
                            MPI_Send( &x[ii - XK_H], iknsupc * nrhs + XK_H,
                                      MPI_DOUBLE, pi, Xk, grid->comm );
#endif
#endif
#if ( DEBUGlevel>=2 )
                            printf("(%2d) Sent X[%2.0f] to P %2d\n",
                                   iam, x[ii - XK_H], pi);
#endif
                        }
                    }
                    xtrsTimer->trsDataSendXY += iknsupc * nrhs + XK_H;
                    /*
                     * Perform local block modifications.
                     */
                    if ( Urbs[lk1] )
                        dlsum_bmod_GG(lsum, x, &x[ii], nrhs, lbmod_buf, gik, bmod, Urbs,
                                      Ucb_indptr, Ucb_valptr, xsup, grid, Llu,
                                      send_req, stat,xtrsTimer);
                } /* if brecv[ik] == 0 */
            }
        } /* if bmod[ik] == 0 */

    } /* for ub ... */

} /* dlSUM_BMOD */










// #ifndef GPUREF
// #define GPUREF 1  
// #endif

/*
 * Sketch of the algorithm for L-solve:
 * =======================
 *
 * Self-scheduling loop:
 *
 *   while ( not finished ) { .. use message counter to control
 *
 *      reveive a message;
 *
 * 	if ( message is Xk ) {
 * 	    perform local block modifications into lsum[];
 *                 lsum[i] -= L_i,k * X[k]
 *          if all local updates done, Isend lsum[] to diagonal process;
 *
 *      } else if ( message is LSUM ) { .. this must be a diagonal process
 *          accumulate LSUM;
 *          if ( all LSUM are received ) {
 *              perform triangular solve for Xi;
 *              Isend Xi down to the current process column;
 *              perform local block modifications into lsum[];
 *          }
 *      }
 *   }
 *
 *
 * Auxiliary data structures: lsum[] / ilsum (pointer to lsum array)
 * =======================
 *
 * lsum[] array (local)
 *   + lsum has "nrhs" columns, row-wise is partitioned by supernodes
 *   + stored by row blocks, column wise storage within a row block
 *   + prepend a header recording the global block number.
 *
 *         lsum[]                        ilsum[nsupers + 1]
 *
 *         -----
 *         | | |  <- header of size 2     ---
 *         --------- <--------------------| |
 *         | | | | |			  ---
 * 	   | | | | |	      |-----------| |
 *         | | | | | 	      |           ---
 *	   ---------          |   |-------| |
 *         | | |  <- header   |   |       ---
 *         --------- <--------|   |  |----| |
 *         | | | | |		  |  |    ---
 * 	   | | | | |              |  |
 *         | | | | |              |  |
 *	   ---------              |  |
 *         | | |  <- header       |  |
 *         --------- <------------|  |
 *         | | | | |                 |
 * 	   | | | | |                 |
 *         | | | | |                 |
 *	   --------- <---------------|
 */

/*#define ISEND_IRECV*/

/*
 * Function prototypes
 */
#ifdef _CRAY
fortran void STRSM(_fcd, _fcd, _fcd, _fcd, int*, int*, double*,
		   double*, int*, double*, int*);
_fcd ftcs1;
_fcd ftcs2;
_fcd ftcs3;
#endif





int_t localSolveXkYk( trtype_t trtype, int_t k, double* x, int_t nrhs,
                      dLUstruct_t * LUstruct, gridinfo_t * grid,
                      SuperLUStat_t * stat)
{
    // printf("Solving %d \n",k );
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu; double alpha = 1.0;
    int_t* xsup = Glu_persist->xsup;
    int_t** Lrowind_bc_ptr = Llu->Lrowind_bc_ptr;
    double** Lnzval_bc_ptr = Llu->Lnzval_bc_ptr;
    int_t knsupc = SuperSize (k);
    int_t lk = LBj (k, grid); /* Local block number, column-wise */
    int_t *lsub = Lrowind_bc_ptr[lk];
    double* lusup = Lnzval_bc_ptr[lk];
    int_t nsupr = lsub[1];

    if (trtype == UPPER_TRI)
    {
        /* upper triangular matrix */
#ifdef _CRAY
        STRSM (ftcs1, ftcs3, ftcs2, ftcs2, &knsupc, &nrhs, &alpha,
               lusup, &nsupr, x, &knsupc);
#elif defined (USE_VENDOR_BLAS)
        dtrsm_ ("L", "U", "N", "N", &knsupc, &nrhs, &alpha,
                lusup, &nsupr, x, &knsupc, 1, 1, 1, 1);
#else
        dtrsm_ ("L", "U", "N", "N", &knsupc, &nrhs, &alpha,
                lusup, &nsupr, x, &knsupc);
#endif
    }
    else
    {
        /* lower triangular matrix */
#ifdef _CRAY
        STRSM (ftcs1, ftcs1, ftcs2, ftcs3, &knsupc, &nrhs, &alpha,
               lusup, &nsupr, x, &knsupc);
#elif defined (USE_VENDOR_BLAS)
        dtrsm_ ("L", "L", "N", "U", &knsupc, &nrhs, &alpha,
                lusup, &nsupr, x, &knsupc, 1, 1, 1, 1);
#else
        dtrsm_ ("L", "L", "N", "U", &knsupc, &nrhs, &alpha,
                lusup, &nsupr, x, &knsupc);
#endif
    }
    stat->ops[SOLVE] += knsupc * (knsupc + 1) * nrhs;
    return 0;
}

int_t iBcastXk2Pck(int_t k, double* x, int_t nrhs,
                   int_t** sendList, MPI_Request *send_req,
                   dLUstruct_t * LUstruct, gridinfo_t * grid,xtrsTimer_t *xtrsTimer)
{
    /*
     * Send Xk to process column Pc[k].
     */
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;
    int_t* xsup = Glu_persist->xsup;
    int_t Pr = grid->nprow;
    int_t knsupc = SuperSize (k);
    int_t lk = LBj (k, grid);
    int_t kcol = PCOL (k, grid);
    for (int_t p = 0; p < Pr; ++p)
    {
        if (sendList[lk][p] != EMPTY)
        {
            int_t pi = PNUM (p, kcol, grid);

            MPI_Isend (x, knsupc * nrhs + XK_H,
                       MPI_DOUBLE, pi, Xk, grid->comm,
                       &send_req[Llu->SolveMsgSent++]);

        }
    }

    xtrsTimer->trsDataSendXY += (double) SuperSize(k)*nrhs + XK_H;
    // printf("Data sent so far =%g and in this round= %g \n",xtrsTimer->trsDataSendXY, (double) SuperSize(k)*nrhs + XK_H );

    return 0;
}

/*! \brief
 *
 * <pre>
 * Purpose
 *
 *   Re-distribute B on the diagonal processes of the 2D process mesh.
 *
 * Note
 *
 *   This routine can only be called after the routine pxgstrs_init(),
 *   in which the structures of the send and receive buffers are set up.
 *
 * Arguments
 *
  *
 * B      (input) double*
 *        The distributed right-hand side matrix of the possibly
 *        equilibrated system.
 *
 * m_loc  (input) int (local)
 *        The local row dimension of matrix B.
 *
 * nrhs   (input) int (global)
 *        Number of right-hand sides.
 *
 * ldb    (input) int (local)
 *        Leading dimension of matrix B.
 *
 * fst_row (input) int (global)
 *        The row number of B's first row in the global matrix.
 *
 * ilsum  (input) int* (global)
 *        Starting position of each supernode in a full array.
 *
 * x      (output) double*
 *        The solution vector. It is valid only on the diagonal processes.
 *
 * ScalePermstruct (input) dScalePermstruct_t*
 *        The data structure to store the scaling and permutation vectors
 *        describing the transformations performed to the original matrix A.
 *
 * grid   (input) gridinfo_t*
 *        The 2D process mesh.
 *
 * SOLVEstruct (input) dSOLVEstruct_t*
 *        Contains the information for the communication during the
 *        solution phase.
 *
 * Return value

 * </pre>
 */

int_t
pdReDistribute3d_B_to_X (double *B, int_t m_loc, int nrhs, int_t ldb,
                         int_t fst_row, int_t * ilsum, double *x,
                         dScalePermstruct_t * ScalePermstruct,
                         Glu_persist_t * Glu_persist,
                         gridinfo3d_t * grid3d, dSOLVEstruct_t * SOLVEstruct)
{
    int *SendCnt, *SendCnt_nrhs, *RecvCnt, *RecvCnt_nrhs;
    int *sdispls, *sdispls_nrhs, *rdispls, *rdispls_nrhs;
    int *ptr_to_ibuf, *ptr_to_dbuf;
    int_t *perm_r, *perm_c;     /* row and column permutation vectors */
    int_t *send_ibuf, *recv_ibuf;
    double *send_dbuf, *recv_dbuf;
    int_t *xsup, *supno;
    int_t i, ii, irow, gbi, jj, k, knsupc, l, lk;
    int p, procs;
    gridinfo_t * grid = &(grid3d->grid2d);
    if (!grid3d->zscp.Iam)
    {
        pxgstrs_comm_t *gstrs_comm = SOLVEstruct->gstrs_comm;

#if ( DEBUGlevel>=1 )
        CHECK_MALLOC (grid->iam, "Enter pdReDistribute_B_to_X()");
#endif

        /* ------------------------------------------------------------
           INITIALIZATION.
           ------------------------------------------------------------ */
        perm_r = ScalePermstruct->perm_r;
        perm_c = ScalePermstruct->perm_c;
        procs = grid->nprow * grid->npcol;
        xsup = Glu_persist->xsup;
        supno = Glu_persist->supno;
        SendCnt = gstrs_comm->B_to_X_SendCnt;
        SendCnt_nrhs = gstrs_comm->B_to_X_SendCnt + procs;
        RecvCnt = gstrs_comm->B_to_X_SendCnt + 2 * procs;
        RecvCnt_nrhs = gstrs_comm->B_to_X_SendCnt + 3 * procs;
        sdispls = gstrs_comm->B_to_X_SendCnt + 4 * procs;
        sdispls_nrhs = gstrs_comm->B_to_X_SendCnt + 5 * procs;
        rdispls = gstrs_comm->B_to_X_SendCnt + 6 * procs;
        rdispls_nrhs = gstrs_comm->B_to_X_SendCnt + 7 * procs;
        ptr_to_ibuf = gstrs_comm->ptr_to_ibuf;
        ptr_to_dbuf = gstrs_comm->ptr_to_dbuf;

        /* ------------------------------------------------------------
           NOW COMMUNICATE THE ACTUAL DATA.
           ------------------------------------------------------------ */
        k = sdispls[procs - 1] + SendCnt[procs - 1];    /* Total number of sends */
        l = rdispls[procs - 1] + RecvCnt[procs - 1];    /* Total number of receives */
        if (!(send_ibuf = intMalloc_dist (k + l)))
            ABORT ("Malloc fails for send_ibuf[].");
        recv_ibuf = send_ibuf + k;
        if (!(send_dbuf = doubleMalloc_dist ((k + l) * (size_t) nrhs)))
            ABORT ("Malloc fails for send_dbuf[].");
        recv_dbuf = send_dbuf + k * nrhs;

        for (p = 0; p < procs; ++p)
        {
            ptr_to_ibuf[p] = sdispls[p];
            ptr_to_dbuf[p] = sdispls[p] * nrhs;
        }

        /* Copy the row indices and values to the send buffer. */
        for (i = 0, l = fst_row; i < m_loc; ++i, ++l)
        {
            irow = perm_c[perm_r[l]];   /* Row number in Pc*Pr*B */
            gbi = BlockNum (irow);
            p = PNUM (PROW (gbi, grid), PCOL (gbi, grid), grid);    /* Diagonal process */
            k = ptr_to_ibuf[p];
            send_ibuf[k] = irow;
            k = ptr_to_dbuf[p];
            for (int_t j = 0; j < nrhs; ++j)
            {
                /* RHS is stored in row major in the buffer. */
                send_dbuf[k++] = B[i + j * ldb];
            }
            ++ptr_to_ibuf[p];
            ptr_to_dbuf[p] += nrhs;
        }

        /* Communicate the (permuted) row indices. */
        MPI_Alltoallv (send_ibuf, SendCnt, sdispls, mpi_int_t,
                       recv_ibuf, RecvCnt, rdispls, mpi_int_t, grid->comm);

        /* Communicate the numerical values. */
        MPI_Alltoallv (send_dbuf, SendCnt_nrhs, sdispls_nrhs, MPI_DOUBLE,
                       recv_dbuf, RecvCnt_nrhs, rdispls_nrhs, MPI_DOUBLE,
                       grid->comm);

        /* ------------------------------------------------------------
           Copy buffer into X on the diagonal processes.
           ------------------------------------------------------------ */
        ii = 0;
        for (p = 0; p < procs; ++p)
        {
            jj = rdispls_nrhs[p];
            for (int_t i = 0; i < RecvCnt[p]; ++i)
            {
                /* Only the diagonal processes do this; the off-diagonal processes
                   have 0 RecvCnt. */
                irow = recv_ibuf[ii];   /* The permuted row index. */
                k = BlockNum (irow);
                knsupc = SuperSize (k);
                lk = LBi (k, grid); /* Local block number. */
                l = X_BLK (lk);
                x[l - XK_H] = k;    /* Block number prepended in the header. */
                irow = irow - FstBlockC (k);    /* Relative row number in X-block */
                for (int_t j = 0; j < nrhs; ++j)
                {
                    x[l + irow + j * knsupc] = recv_dbuf[jj++];
                }
                ++ii;
            }
        }

        SUPERLU_FREE (send_ibuf);
        SUPERLU_FREE (send_dbuf);
    }
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC (grid->iam, "Exit pdReDistribute_B_to_X()");
#endif
    return 0;
}                               /* pdReDistribute3d_B_to_X */

/*! \brief
 *
 * <pre>
 * Purpose
 *
 *   Re-distribute X on the diagonal processes to B distributed on all
 *   the processes.
 *
 * Note
 *
 *   This routine can only be called after the routine pxgstrs_init(),
 *   in which the structures of the send and receive buffers are set up.
 * </pre>
 */

int_t
pdReDistribute3d_X_to_B (int_t n, double *B, int_t m_loc, int_t ldb,
                         int_t fst_row, int_t nrhs, double *x, int_t * ilsum,
                         dScalePermstruct_t * ScalePermstruct,
                         Glu_persist_t * Glu_persist, gridinfo3d_t * grid3d,
                         dSOLVEstruct_t * SOLVEstruct)
{
    int_t i, ii, irow,  jj, k, knsupc, nsupers, l, lk;
    int_t *xsup, *supno;
    int *SendCnt, *SendCnt_nrhs, *RecvCnt, *RecvCnt_nrhs;
    int *sdispls, *rdispls, *sdispls_nrhs, *rdispls_nrhs;
    int *ptr_to_ibuf, *ptr_to_dbuf;
    int_t *send_ibuf, *recv_ibuf;
    double *send_dbuf, *recv_dbuf;
    int iam, p, q, pkk, procs;
    int_t num_diag_procs, *diag_procs;
    gridinfo_t * grid = &(grid3d->grid2d);
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC (grid->iam, "Enter pdReDistribute_X_to_B()");
#endif

    /* ------------------------------------------------------------
       INITIALIZATION.
       ------------------------------------------------------------ */
    xsup = Glu_persist->xsup;
    supno = Glu_persist->supno;
    nsupers = Glu_persist->supno[n - 1] + 1;
    iam = grid->iam;
    procs = grid->nprow * grid->npcol;
    if (!grid3d->zscp.Iam)
    {
        int_t *row_to_proc = SOLVEstruct->row_to_proc;  /* row-process mapping */
        pxgstrs_comm_t *gstrs_comm = SOLVEstruct->gstrs_comm;

        SendCnt = gstrs_comm->X_to_B_SendCnt;
        SendCnt_nrhs = gstrs_comm->X_to_B_SendCnt + procs;
        RecvCnt = gstrs_comm->X_to_B_SendCnt + 2 * procs;
        RecvCnt_nrhs = gstrs_comm->X_to_B_SendCnt + 3 * procs;
        sdispls = gstrs_comm->X_to_B_SendCnt + 4 * procs;
        sdispls_nrhs = gstrs_comm->X_to_B_SendCnt + 5 * procs;
        rdispls = gstrs_comm->X_to_B_SendCnt + 6 * procs;
        rdispls_nrhs = gstrs_comm->X_to_B_SendCnt + 7 * procs;
        ptr_to_ibuf = gstrs_comm->ptr_to_ibuf;
        ptr_to_dbuf = gstrs_comm->ptr_to_dbuf;

        k = sdispls[procs - 1] + SendCnt[procs - 1];    /* Total number of sends */
        l = rdispls[procs - 1] + RecvCnt[procs - 1];    /* Total number of receives */
        if (!(send_ibuf = intMalloc_dist (k + l)))
            ABORT ("Malloc fails for send_ibuf[].");
        recv_ibuf = send_ibuf + k;
        if (!(send_dbuf = doubleMalloc_dist ((k + l) * nrhs)))
            ABORT ("Malloc fails for send_dbuf[].");
        recv_dbuf = send_dbuf + k * nrhs;
        for (p = 0; p < procs; ++p)
        {
            ptr_to_ibuf[p] = sdispls[p];
            ptr_to_dbuf[p] = sdispls_nrhs[p];
        }
        num_diag_procs = SOLVEstruct->num_diag_procs;
        diag_procs = SOLVEstruct->diag_procs;

        for (p = 0; p < num_diag_procs; ++p)
        {
            /* For all diagonal processes. */
            pkk = diag_procs[p];
            if (iam == pkk)
            {
                for (k = p; k < nsupers; k += num_diag_procs)
                {
                    knsupc = SuperSize (k);
                    lk = LBi (k, grid); /* Local block number */
                    irow = FstBlockC (k);
                    l = X_BLK (lk);
                    for (i = 0; i < knsupc; ++i)
                    {

                        ii = irow;

                        q = row_to_proc[ii];
                        jj = ptr_to_ibuf[q];
                        send_ibuf[jj] = ii;
                        jj = ptr_to_dbuf[q];
                        for (int_t j = 0; j < nrhs; ++j)
                        {
                            /* RHS stored in row major in buffer. */
                            send_dbuf[jj++] = x[l + i + j * knsupc];
                        }
                        ++ptr_to_ibuf[q];
                        ptr_to_dbuf[q] += nrhs;
                        ++irow;
                    }
                }
            }
        }

        /* ------------------------------------------------------------
           COMMUNICATE THE (PERMUTED) ROW INDICES AND NUMERICAL VALUES.
           ------------------------------------------------------------ */
        MPI_Alltoallv (send_ibuf, SendCnt, sdispls, mpi_int_t,
                       recv_ibuf, RecvCnt, rdispls, mpi_int_t, grid->comm);
        MPI_Alltoallv (send_dbuf, SendCnt_nrhs, sdispls_nrhs, MPI_DOUBLE,
                       recv_dbuf, RecvCnt_nrhs, rdispls_nrhs, MPI_DOUBLE,
                       grid->comm);

        /* ------------------------------------------------------------
           COPY THE BUFFER INTO B.
           ------------------------------------------------------------ */
        for (i = 0, k = 0; i < m_loc; ++i)
        {
            irow = recv_ibuf[i];
            irow -= fst_row;        /* Relative row number */
            for (int_t j = 0; j < nrhs; ++j)
            {
                /* RHS is stored in row major in the buffer. */
                B[irow + j * ldb] = recv_dbuf[k++];
            }
        }

        SUPERLU_FREE (send_ibuf);
        SUPERLU_FREE (send_dbuf);
    }
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC (grid->iam, "Exit pdReDistribute_X_to_B()");
#endif
    return 0;

}                               /* pdReDistribute_X_to_B */

int_t* getfmod(int_t nlb, dLocalLU_t *Llu)
{
    int_t* fmod;
    if (!(fmod = intMalloc_dist (nlb)))
        ABORT ("Calloc fails for fmod[].");
    for (int_t i = 0; i < nlb; ++i)
        fmod[i] = Llu->fmod[i];
    return fmod;
}



/*! \brief
 *
 * <pre>
 * Purpose
 *
 *
 * PDGSTRS solves a system of distributed linear equations
 * A*X = B with a general N-by-N matrix A using the LU factorization
 * computed by PDGSTRF.
 * If the equilibration, and row and column permutations were performed,
 * the LU factorization was performed for A1 where
 *     A1 = Pc*Pr*diag(R)*A*diag(C)*Pc^T = L*U
 * and the linear system solved is
 *     A1 * Y = Pc*Pr*B1, where B was overwritten by B1 = diag(R)*B, and
 * the permutation to B1 by Pc*Pr is applied internally in this routine.
 *
 * Arguments
 *
 *
 * n      (input) int (global)
 *        The order of the system of linear equations.
 *
 * LUstruct (input) dLUstruct_t*
 *        The distributed data structures storing L and U factors.
 *        The L and U factors are obtained from PDGSTRF for
 *        the possibly scaled and permuted matrix A.
 *        See superlu_ddefs.h for the definition of 'dLUstruct_t'.
 *        A may be scaled and permuted into A1, so that
 *        A1 = Pc*Pr*diag(R)*A*diag(C)*Pc^T = L*U
 *
 * grid   (input) gridinfo_t*
 *        The 2D process mesh. It contains the MPI communicator, the number
 *        of process rows (NPROW), the number of process columns (NPCOL),
 *        and my process rank. It is an input argument to all the
 *        parallel routines.
 *        Grid can be initialized by subroutine SUPERLU_GRIDINIT.
 *        See superlu_defs.h for the definition of 'gridinfo_t'.
 *
 * B      (input/output) double*
 *        On entry, the distributed right-hand side matrix of the possibly
 *        equilibrated system. That is, B may be overwritten by diag(R)*B.
 *        On exit, the distributed solution matrix Y of the possibly
 *        equilibrated system if info = 0, where Y = Pc*diag(C)^(-1)*X,
 *        and X is the solution of the original system.
 *
 * m_loc  (input) int (local)
 *        The local row dimension of matrix B.
 *
 * fst_row (input) int (global)
 *        The row number of B's first row in the global matrix.
 *
 * ldb    (input) int (local)
 *        The leading dimension of matrix B.
 *
 * nrhs   (input) int (global)
 *        Number of right-hand sides.
 *
 * SOLVEstruct (input) dSOLVEstruct_t* (global)
 *        Contains the information for the communication during the
 *        solution phase.
 *
 * stat   (output) SuperLUStat_t*
 *        Record the statistics about the triangular solves.
 *        See util.h for the definition of 'SuperLUStat_t'.
 *
 * info   (output) int*
 *     = 0: successful exit
 *     < 0: if info = -i, the i-th argument had an illegal value
 * </pre>
 */

void
pdgstrs3d (superlu_dist_options_t *options, int_t n, dLUstruct_t * LUstruct,
           dScalePermstruct_t * ScalePermstruct,
           dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d, double *B,
           int_t m_loc, int_t fst_row, int_t ldb, int nrhs,
           dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, int *info)
{
    // printf("Using pdgstr3d ..\n");
    gridinfo_t * grid = &(grid3d->grid2d);
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;

    double *lsum;               /* Local running sum of the updates to B-components */
    double *x;                  /* X component at step k. */
    /* NOTE: x and lsum are of same size. */

    double *recvbuf;


    int_t iam,  mycol, myrow;
    int_t i, k;
    int_t  nlb, nsupers;
    int_t *xsup, *supno;
    int_t *ilsum;               /* Starting position of each supernode in lsum (LOCAL) */
    int_t Pc, Pr;
    int knsupc;
    int ldalsum;                /* Number of lsum entries locally owned. */
    int maxrecvsz;
    int_t **Lrowind_bc_ptr;
    double **Lnzval_bc_ptr;
    MPI_Status status;
    MPI_Request *send_req;


    double t;
#if ( DEBUGlevel>=2 )
    int_t Ublocks = 0;
#endif



    t = SuperLU_timer_ ();

    /* Test input parameters. */
    *info = 0;
    if ( n < 0 ) *info = -1;
    else if ( nrhs < 0 ) *info = -9;
    if ( *info ) {
	pxerr_dist("PDGSTRS", grid, -*info);
	return;
    }
#ifdef _CRAY
    ftcs1 = _cptofcd ("L", strlen ("L"));
    ftcs2 = _cptofcd ("N", strlen ("N"));
    ftcs3 = _cptofcd ("U", strlen ("U"));
#endif

    /*
     * Initialization.
     */
    iam = grid->iam;
    Pc = grid->npcol;
    Pr = grid->nprow;
    myrow = MYROW (iam, grid);
    mycol = MYCOL (iam, grid);
    xsup = Glu_persist->xsup;
    supno = Glu_persist->supno;
    nsupers = supno[n - 1] + 1;
    Lrowind_bc_ptr = Llu->Lrowind_bc_ptr;
    Lnzval_bc_ptr = Llu->Lnzval_bc_ptr;
    nlb = CEILING (nsupers, Pr);    /* Number of local block rows. */
    int_t nub = CEILING (nsupers, Pc);

#if ( DEBUGlevel>=1 )
    CHECK_MALLOC (iam, "Enter pdgstrs()");
#endif

    stat->ops[SOLVE] = 0.0;
    Llu->SolveMsgSent = 0;

    MPI_Bcast( &(Llu->nfsendx), 1, mpi_int_t, 0,  grid3d->zscp.comm);
    MPI_Bcast( &(Llu->nbsendx), 1, mpi_int_t, 0,  grid3d->zscp.comm);
    MPI_Bcast( &(Llu->ldalsum), 1, mpi_int_t, 0,  grid3d->zscp.comm);
    zAllocBcast(nlb * sizeof(int_t), &(Llu->ilsum), grid3d);
    zAllocBcast(nlb * sizeof(int_t), &(Llu->fmod), grid3d);
    zAllocBcast(nlb * sizeof(int_t), &(Llu->bmod), grid3d);
    zAllocBcast(nlb * sizeof(int_t), &(Llu->mod_bit), grid3d);
    zAllocBcast(2 * nub * sizeof(int_t), &(Llu->Urbs), grid3d);
    int_t* Urbs = Llu->Urbs;
    if (grid3d->zscp.Iam)
    {
        Llu->Ucb_indptr = SUPERLU_MALLOC (nub * sizeof(Ucb_indptr_t*));
        Llu->Ucb_valptr = SUPERLU_MALLOC (nub * sizeof(int_t*));
        Llu->bsendx_plist = SUPERLU_MALLOC (nub * sizeof(int_t*));
        Llu->fsendx_plist = SUPERLU_MALLOC (nub * sizeof(int_t*));
    }

    for (int_t lb = 0; lb < nub; ++lb)
    {
        if (Urbs[lb])
        {
            zAllocBcast(Urbs[lb] * sizeof (Ucb_indptr_t), &(Llu->Ucb_indptr[lb]), grid3d);
            zAllocBcast(Urbs[lb] * sizeof (int_t), &(Llu->Ucb_valptr[lb]), grid3d);
        }
    }

    for (int_t k = 0; k < nsupers; ++k)
    {
        /* code */
        int_t krow = PROW(k, grid);
        int_t kcol = PCOL(k, grid);
        if (myrow == krow && mycol == kcol)
        {
            int_t lk = LBj(k, grid);
            zAllocBcast(Pr * sizeof (int_t), &(Llu->bsendx_plist[lk]), grid3d);
            zAllocBcast(Pr * sizeof (int_t), &(Llu->fsendx_plist[lk]), grid3d);

        }
    }

    k = SUPERLU_MAX (Llu->nfsendx, Llu->nbsendx) + nlb;
    if (!
            (send_req =
                 (MPI_Request *) SUPERLU_MALLOC (k * sizeof (MPI_Request))))
        ABORT ("Malloc fails for send_req[].");




    /* Obtain ilsum[] and ldalsum for process column 0. */


    ilsum = Llu->ilsum;
    ldalsum = Llu->ldalsum;

    /* Allocate working storage. */
    knsupc = sp_ienv_dist (3,options);
    maxrecvsz = knsupc * nrhs + SUPERLU_MAX (XK_H, LSUM_H);
    if (!
            (lsum = doubleCalloc_dist (((size_t) ldalsum) * nrhs + nlb * LSUM_H)))
        ABORT ("Calloc fails for lsum[].");
    if (!(x = doubleMalloc_dist (ldalsum * nrhs + nlb * XK_H)))
        ABORT ("Malloc fails for x[].");
    if (!(recvbuf = doubleMalloc_dist (maxrecvsz)))
        ABORT ("Malloc fails for recvbuf[].");

    /**
     * Initializing xT
     */

    int_t* ilsumT = SUPERLU_MALLOC (sizeof(int_t) * (nub + 1));
    int_t ldaspaT = 0;
    ilsumT[0] = 0;
    for (int_t jb = 0; jb < nsupers; ++jb)
    {
        if ( mycol == PCOL( jb, grid ) )
        {
            int_t i = SuperSize( jb );
            ldaspaT += i;
            int_t ljb = LBj( jb, grid );
            ilsumT[ljb + 1] = ilsumT[ljb] + i;
        }
    }
    double* xT;
    if (!(xT = doubleMalloc_dist (ldaspaT * nrhs + nub * XK_H)))
        ABORT ("Malloc fails for xT[].");
    /**
     * Setup the headers for xT
     */
    for (int_t jb = 0; jb < nsupers; ++jb)
    {
        if ( mycol == PCOL( jb, grid ) )
        {
            int_t ljb = LBj( jb, grid );
            int_t jj = XT_BLK (ljb);
            xT[jj] = jb;

        }
    }

    xT_struct xT_s;
    xT_s.xT = xT;
    xT_s.ilsumT = ilsumT;
    xT_s.ldaspaT = ldaspaT;

    xtrsTimer_t xtrsTimer;

    initTRStimer(&xtrsTimer, grid);
    double tx = SuperLU_timer_();
    /* Redistribute B into X on the diagonal processes. */
    pdReDistribute3d_B_to_X(B, m_loc, nrhs, ldb, fst_row, ilsum, x,
                            ScalePermstruct, Glu_persist, grid3d, SOLVEstruct);

    xtrsTimer.t_pxReDistribute_B_to_X = SuperLU_timer_() - tx;

    /*---------------------------------------------------
     * Forward solve Ly = b.
     *---------------------------------------------------*/

    trs_B_init3d(nsupers, x, nrhs, LUstruct, grid3d);

    MPI_Barrier (grid3d->comm);
    tx = SuperLU_timer_();
    stat->utime[SOLVE] = 0.0;
    double tx_st= SuperLU_timer_();
    
    
    pdgsTrForwardSolve3d(options, n,  LUstruct, ScalePermstruct, trf3Dpartition, grid3d, x,  lsum, &xT_s,
                          recvbuf, send_req,  nrhs, SOLVEstruct,  stat, &xtrsTimer);
    // pdgsTrForwardSolve3d_2d( n,  LUstruct, ScalePermstruct, trf3Dpartition, grid3d, x,  lsum, &xT_s,
    //                          recvbuf, send_req,  nrhs, SOLVEstruct,  stat, info);
    xtrsTimer.t_forwardSolve = SuperLU_timer_() - tx;


    

    /*---------------------------------------------------
     * Back solve Ux = y.
     *
     * The Y components from the forward solve is already
     * on the diagonal processes.
     *---------------------------------------------------*/
    tx = SuperLU_timer_();
    pdgsTrBackSolve3d(options, n,  LUstruct, ScalePermstruct, trf3Dpartition, grid3d, x,  lsum, &xT_s,
                       recvbuf, send_req,  nrhs, SOLVEstruct,  stat, &xtrsTimer);
    // pdgsTrBackSolve3d_2d( n,  LUstruct, ScalePermstruct, trf3Dpartition, grid3d, x,  lsum, &xT_s,
    //                       recvbuf, send_req,  nrhs, SOLVEstruct,  stat, info);
    
    xtrsTimer.t_backwardSolve = SuperLU_timer_() - tx;
    MPI_Barrier (grid3d->comm);
    stat->utime[SOLVE] = SuperLU_timer_ () - tx_st;
    trs_X_gather3d(x, nrhs, trf3Dpartition, LUstruct, grid3d );
    tx = SuperLU_timer_();
    pdReDistribute3d_X_to_B(n, B, m_loc, ldb, fst_row, nrhs, x, ilsum,
                            ScalePermstruct, Glu_persist, grid3d, SOLVEstruct);

    xtrsTimer.t_pxReDistribute_X_to_B = SuperLU_timer_() - tx;

    /**
     * Reduce the Solve flops from all the grids to grid zero
     */
    reduceStat(SOLVE, stat, grid3d);
    /* Deallocate storage. */
    SUPERLU_FREE (lsum);
    SUPERLU_FREE (x);
    SUPERLU_FREE (recvbuf);


    /*for (i = 0; i < Llu->SolveMsgSent; ++i) MPI_Request_free(&send_req[i]); */

    for (i = 0; i < Llu->SolveMsgSent; ++i)
        MPI_Wait (&send_req[i], &status);
    SUPERLU_FREE (send_req);

    MPI_Barrier (grid->comm);

    
    printTRStimer(&xtrsTimer, grid3d);
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC (iam, "Exit pdgstrs()");
#endif

    return;
}                               /* PDGSTRS */




int_t pdgsTrForwardSolve3d(superlu_dist_options_t *options, int_t n, dLUstruct_t * LUstruct,
                           dScalePermstruct_t * ScalePermstruct,
                           dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                           double *x3d, double *lsum3d,
                           xT_struct *xT_s,
                           double * recvbuf,
                           MPI_Request * send_req, int nrhs,
                           dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{
    gridinfo_t * grid = &(grid3d->grid2d);
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;  double zero = 0.0;
    int_t* xsup = Glu_persist->xsup;

    int_t nsupers = Glu_persist->supno[n - 1] + 1;
    int_t Pr = grid->nprow;
    int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);
    int_t **Ufstnz_br_ptr = Llu->Ufstnz_br_ptr;

    int_t* myZeroTrIdxs = trf3Dpartition->myZeroTrIdxs;
    sForest_t** sForests = trf3Dpartition->sForests;
    int_t* myTreeIdxs = trf3Dpartition->myTreeIdxs;
    int_t maxLvl = log2i(grid3d->zscp.Np) + 1;

    int_t *ilsum = Llu->ilsum;

    int_t knsupc = sp_ienv_dist (3,options);
    int_t maxrecvsz = knsupc * nrhs + SUPERLU_MAX (XK_H, LSUM_H);
    double* rtemp;
    if (!(rtemp = doubleCalloc_dist (maxrecvsz)))
        ABORT ("Malloc fails for rtemp[].");

    /**
     *  Loop over all the levels from root to leaf
     */
    int_t ii = 0;
    for (int_t k = 0; k < nsupers; ++k)
    {
        int_t knsupc = SuperSize (k);
        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t lk = LBi (k, grid); /* Local block number. */
            int_t il = LSUM_BLK (lk);
            lsum3d[il - LSUM_H] = k;  /* Block number prepended in the header. */
        }
        ii += knsupc;
    }

    /*initilize lsum to zero*/
    for (int_t k = 0; k < nsupers; ++k)
    {
        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t knsupc = SuperSize (k);
            int_t lk = LBi (k, grid);
            int_t il = LSUM_BLK (lk);
            double* dest = &lsum3d[il];
            for (int_t j = 0; j < nrhs; ++j)
            {
                for (int_t i = 0; i < knsupc; ++i)
                    dest[i + j * knsupc] = zero;
            }
        }
    }

    Llu->SolveMsgSent = 0;
    for (int_t ilvl = 0; ilvl < maxLvl; ++ilvl)
    {
        double tx = SuperLU_timer_();
        /* if I participate in this level */
        if (!myZeroTrIdxs[ilvl])
        {
            int_t tree = myTreeIdxs[ilvl];

            sForest_t* sforest = sForests[myTreeIdxs[ilvl]];

            /*main loop over all the super nodes*/
            if (sforest)
            {
                if (ilvl == 0)
                    leafForestForwardSolve3d(options, tree, n, LUstruct,
                                              ScalePermstruct, trf3Dpartition, grid3d,
                                              x3d,  lsum3d, recvbuf, rtemp,
                                              send_req,  nrhs, SOLVEstruct,  stat, xtrsTimer);
                else
                    nonLeafForestForwardSolve3d(tree, LUstruct,
                                                ScalePermstruct, trf3Dpartition, grid3d,  x3d,  lsum3d, xT_s, recvbuf, rtemp,
                                                send_req, nrhs, SOLVEstruct,  stat, xtrsTimer);

            }
            if (ilvl != maxLvl - 1)
            {
                /* code */
                int_t myGrid = grid3d->zscp.Iam;


                int_t sender, receiver;
                if ((myGrid % (1 << (ilvl + 1))) == 0)
                {
                    sender = myGrid + (1 << ilvl);
                    receiver = myGrid;
                }
                else
                {

                    sender = myGrid;
                    receiver = myGrid - (1 << ilvl);
                }
                double tx = SuperLU_timer_();
                for (int_t alvl = ilvl + 1; alvl <  maxLvl; ++alvl)
                {
                    /* code */
                    int_t treeId = myTreeIdxs[alvl];
                    fsolveReduceLsum3d(treeId, sender, receiver, lsum3d, recvbuf, nrhs,
                                       trf3Dpartition, LUstruct, grid3d,xtrsTimer );
                }
                xtrsTimer->tfs_comm += SuperLU_timer_() - tx;
            }
        }
        xtrsTimer->tfs_tree[ilvl] = SuperLU_timer_() - tx;
    }

    double tx = SuperLU_timer_();
    for (int_t i = 0; i < Llu->SolveMsgSent; ++i)
    {
        MPI_Status status;
        MPI_Wait (&send_req[i], &status);
    }
    Llu->SolveMsgSent = 0;
    xtrsTimer->tfs_comm += SuperLU_timer_() - tx;

    return 0;
}



int_t pdgsTrBackSolve3d(superlu_dist_options_t *options, int_t n, dLUstruct_t * LUstruct,
                        dScalePermstruct_t * ScalePermstruct,
                        dtrf3Dpartition_t*  trf3Dpartition, gridinfo3d_t *grid3d,
                        double *x3d, double *lsum3d,
                        xT_struct *xT_s,
                        double * recvbuf,
                        MPI_Request * send_req, int nrhs,
                        dSOLVEstruct_t * SOLVEstruct, SuperLUStat_t * stat, xtrsTimer_t *xtrsTimer)
{
    // printf("Using pdgsTrBackSolve3d_2d \n");

    gridinfo_t * grid = &(grid3d->grid2d);
    Glu_persist_t *Glu_persist = LUstruct->Glu_persist;
    dLocalLU_t *Llu = LUstruct->Llu;  double zero = 0.0;
    int_t* xsup = Glu_persist->xsup;

    int_t nsupers = Glu_persist->supno[n - 1] + 1;
    int_t Pr = grid->nprow;
    int_t Pc = grid->npcol;
    int_t iam = grid->iam;
    int_t myrow = MYROW (iam, grid);
    int_t mycol = MYCOL (iam, grid);
    int_t **Ufstnz_br_ptr = Llu->Ufstnz_br_ptr;

    int_t* myZeroTrIdxs = trf3Dpartition->myZeroTrIdxs;
    sForest_t** sForests = trf3Dpartition->sForests;
    int_t* myTreeIdxs = trf3Dpartition->myTreeIdxs;
    int_t maxLvl = log2i(grid3d->zscp.Np) + 1;

    int_t *ilsum = Llu->ilsum;

    /**
     *  Loop over all the levels from root to leaf
     */

    /*initilize lsum to zero*/
    for (int_t k = 0; k < nsupers; ++k)
    {
        int_t krow = PROW (k, grid);
        if (myrow == krow)
        {
            int_t knsupc = SuperSize (k);
            int_t lk = LBi (k, grid);
            int_t il = LSUM_BLK (lk);
            double* dest = &lsum3d[il];
            for (int_t j = 0; j < nrhs; ++j)
            {
                for (int_t i = 0; i < knsupc; ++i)
                    dest[i + j * knsupc] = zero;
            }
        }
    }

    /**
     * Adding lsumBmod_buff_t* lbmod_buf
     */

    lsumBmod_buff_t lbmod_buf;
    int_t nsupc = sp_ienv_dist (3,options);
    initLsumBmod_buff(nsupc, nrhs, &lbmod_buf);

    int_t numTrees = 2 * grid3d->zscp.Np - 1;
    int_t nLeafTrees = grid3d->zscp.Np;
    Llu->SolveMsgSent = 0;
    for (int_t ilvl = maxLvl - 1; ilvl >= 0  ; --ilvl)
    {
        /* code */
        double tx = SuperLU_timer_();
        if (!myZeroTrIdxs[ilvl])
        {
            double tx = SuperLU_timer_();
            bsolve_Xt_bcast(ilvl, xT_s, nrhs, trf3Dpartition,
                            LUstruct, grid3d,xtrsTimer );
            xtrsTimer->tbs_comm += SuperLU_timer_() - tx;


            int_t tree = myTreeIdxs[ilvl];

            int_t trParent = (tree + 1) / 2  - 1;
            tx = SuperLU_timer_();
            while (trParent > -1 )
            {
                dlasum_bmod_Tree(trParent, tree, lsum3d, x3d,  xT_s, nrhs, &lbmod_buf,
                                 LUstruct, trf3Dpartition, grid3d, stat);
                trParent = (trParent + 1) / 2 - 1;

            }
            xtrsTimer->tbs_compute += SuperLU_timer_() - tx;


            sForest_t* sforest = sForests[myTreeIdxs[ilvl]];

            /*main loop over all the super nodes*/
            if (sforest)
            {
                if (ilvl == 0)
                    leafForestBackSolve3d(options, tree, n, LUstruct,
                                           ScalePermstruct, trf3Dpartition, grid3d, x3d,  lsum3d, recvbuf,
                                           send_req,  nrhs, &lbmod_buf,
                                            SOLVEstruct,  stat, xtrsTimer);
                else
                    nonLeafForestBackSolve3d(tree, LUstruct,
                                             ScalePermstruct, trf3Dpartition, grid3d,  x3d,  lsum3d, xT_s, recvbuf,
                                             send_req, nrhs, &lbmod_buf,
                                              SOLVEstruct,  stat, xtrsTimer);


            }
        }
        xtrsTimer->tbs_tree[ilvl] = SuperLU_timer_() - tx;
    }
    double tx = SuperLU_timer_();
    for (int_t i = 0; i < Llu->SolveMsgSent; ++i)
    {
        MPI_Status status;
        MPI_Wait (&send_req[i], &status);
    }
    xtrsTimer->tbs_comm += SuperLU_timer_() - tx;
    Llu->SolveMsgSent = 0;


    return 0;
}