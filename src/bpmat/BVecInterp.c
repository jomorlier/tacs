#include "BVecInterp.h"
#include "FElibrary.h"
#include "MatUtils.h"

/*!
  BVecInterp: Interpolate with constant weights between two vectors. 

  Copyright (c) 2014 Graeme Kennedy. All rights reserved.
  Not for commercial purposes.
*/

/*
  These are the definitions for the generic and block-specific
  matrix-vector operations required in the BVecInterp class.
*/
void BVecInterpMultAddGen( int bsize, int nrows, 
			   const int * rowp, const int * cols,
			   const TacsScalar * weights,
			   const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAddGen( int bsize, int nrows, 
				    const int * rowp, const int * cols,
				    const TacsScalar * weights,
				    const TacsScalar * x, TacsScalar * y );

void BVecInterpMultAdd1( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * weights,
			 const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAdd1( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * weights,
				  const TacsScalar * x, TacsScalar * y );

void BVecInterpMultAdd2( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * weights,
			 const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAdd2( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * weights,
				  const TacsScalar * x, TacsScalar * y );

void BVecInterpMultAdd3( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * weights,
			 const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAdd3( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * weights,
				  const TacsScalar * x, TacsScalar * y );

void BVecInterpMultAdd5( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * weights,
			 const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAdd5( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * weights,
				  const TacsScalar * x, TacsScalar * y );

void BVecInterpMultAdd6( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * weights,
			 const TacsScalar * x, TacsScalar * y );
void BVecInterpMultTransposeAdd6( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * weights,
				  const TacsScalar * x, TacsScalar * y );

/*
  This object represents a matrix that interpolates between
  vectors of different sizes. Each interpolation is performed block-wise
  in the sense that the same weights are applied to each component 
  of the block.

  This code works in the following manner: 

  1. The input/output maps of the operator are defined
  2. Each processor adds an interpolation between the input and output
  3. A call to finalize() is made to finish initialization
  4. Calls can be made to mult, multAdd, multTranspose, and multTransposeAdd
*/
BVecInterp::BVecInterp( VarMap * in, VarMap * out ){
  inMap = in;
  inMap->incref();

  outMap = out;
  outMap->incref();
  
  bsize = inMap->getBlockSize();
  if (bsize != outMap->getBlockSize()){
    fprintf(stderr, "Error in BVecInterp: block sizes do not match\n");
    return;
  }

  // Make sure that the MPI communicators from the input/output
  // vectors are the same
  int result;
  MPI_Comm_compare(out->getMPIComm(), in->getMPIComm(), &result);
  
  if (!(result == MPI_IDENT || result == MPI_CONGRUENT)){
    fprintf(stderr, "Error in BVecInterp: MPI groups are not idential \
or congruent. Cannot form interpolant.\n");
    return;
  }

  // Record the communicator for later usage
  comm = inMap->getMPIComm();

  // Determine the range of ownership for the input/output
  int mpiSize;
  outMap->getOwnerRange(&outOwnerRange, &mpiRank, &mpiSize);
  inMap->getOwnerRange(&inOwnerRange, &mpiRank, &mpiSize);

  // Initialize the on- and off-processor temporary storage that 
  // will be used to store the interpolation weights
  N = outMap->getDim();

  on_size = 0;
  max_on_size = N;
  max_on_weights = 27*max_on_size;
  on_nums = new int[ max_on_size ];
  on_rowp = new int[ max_on_size+1 ];
  on_vars = new int[ max_on_weights ];
  on_weights = new TacsScalar[ max_on_weights ];
  on_rowp[0] = 0;

  off_size = 0;
  max_off_size = int(0.1*N);
  if (max_off_size < 100){
    max_off_size = 100;
  }
  max_off_weights = 27*max_off_size;
  off_nums = new int[ max_off_size ];
  off_rowp = new int[ max_off_size+1 ];
  off_vars = new int[ max_off_weights ];
  off_weights = new TacsScalar[ max_off_weights ];
  off_rowp[0] = 0;

  // For now, set the interpolation data to null
  vecDist = NULL;
  rowp = NULL;
  cols = NULL;
  weights = NULL;

  ext_rowp = NULL;
  ext_cols = NULL;
  ext_weights = NULL;
  x_ext = NULL;

  // Initialize the implementation
  multadd = BVecInterpMultAddGen;
  multtransadd = BVecInterpMultTransposeAddGen;

  // Initialize the block-specific implementations
  switch (bsize) {
  case 1:
    multadd = BVecInterpMultAdd1;
    multtransadd = BVecInterpMultTransposeAdd1;
    break;
  case 2:
    multadd = BVecInterpMultAdd2;
    multtransadd = BVecInterpMultTransposeAdd2;
    break;
  case 3:
    multadd = BVecInterpMultAdd3;
    multtransadd = BVecInterpMultTransposeAdd3;
    break;
  case 5:
    multadd = BVecInterpMultAdd5;
    multtransadd = BVecInterpMultTransposeAdd5;
    break;
  case 6:
    multadd = BVecInterpMultAdd6;
    multtransadd = BVecInterpMultTransposeAdd6;
    break;
  default:
    break;
  }
}

/*
  Delete the interpolation object, and free all the objects allocated
  internally.  
*/
BVecInterp::~BVecInterp(){
  inMap->decref();
  outMap->decref();

  // Deallocate data that may have been freed in finalize()
  if (on_nums){ delete [] on_nums; }
  if (on_rowp){ delete [] on_rowp; }
  if (on_vars){ delete [] on_vars; }
  if (on_weights){ delete [] on_weights; }

  if (off_nums){ delete [] off_nums; }
  if (off_rowp){ delete [] off_rowp; }
  if (off_vars){ delete [] off_vars; }
  if (off_weights){ delete [] off_weights; }

  // Deallocate data that may have been allocated in finalize()
  if (vecDist){ vecDist->decref(); }
  if (rowp){ delete [] rowp; }
  if (cols){ delete [] cols; }
  if (weights){ delete [] weights; }
  if (x_ext){ delete [] x_ext; }
  if (ext_rowp){ delete [] ext_rowp; }
  if (ext_cols){ delete [] ext_cols; }
  if (ext_weights){ delete [] ext_weights; }
}

/*
  Add an interopolation between an output variable, and a series of
  input variables. Variables can be added from anywhere to anywhere,
  but it is more efficient if variables are primarily added on the
  processors to which they belong.
  
  This should be called for every variable in the
  interpolation/extrapolation.

  input:
  num:      the interpolation variable number
  weights:  the interpolation weights
  vars:     the interpolating variable numbers
  size:     the number of points used in the interpolation
*/
void BVecInterp::addInterp( int num, TacsScalar w[], 
			    int vars[], int size ){
  if (num >= outOwnerRange[mpiRank] &&
      num <  outOwnerRange[mpiRank+1]){
    // This code stores the values locally until the finalize call is
    // made. First, check if the current size of the allocated memory is
    // sufficient to store the interpolation. If not, allocate larger
    // arrays.
    if (on_size >= max_on_size){
      // Double the current size of the array and copy the old 
      // values into the newly allocated part.
      max_on_size += max_on_size;
      int *tmp_nums = on_nums;
      int *tmp_rowp = on_rowp;
      on_nums = new int[ max_on_size ];
      on_rowp = new int[ max_on_size+1 ];

      memcpy(on_nums, tmp_nums, on_size*sizeof(int));
      memcpy(on_rowp, tmp_rowp, (on_size+1)*sizeof(int));
      delete [] tmp_nums;
      delete [] tmp_rowp;
    }
    if (on_rowp[on_size] + size > max_on_weights){
      // Increase the size of the working pointer array, and
      // copy over the old values into the new array
      max_on_weights += size + max_on_weights;
      int *tmp_vars = on_vars;
      TacsScalar *tmp_weights = on_weights;
      on_vars = new int[ max_on_weights ];
      on_weights = new TacsScalar[ max_on_weights ];

      memcpy(on_vars, tmp_vars, on_rowp[on_size]*sizeof(int));
      memcpy(on_weights, tmp_weights, on_rowp[on_size]*sizeof(TacsScalar));
      delete [] tmp_vars;
      delete [] tmp_weights;
    }
    
    // Store the values temporarily 
    on_nums[on_size] = num;
    on_rowp[on_size+1] = on_rowp[on_size] + size;
    for ( int i = 0, j = on_rowp[on_size]; i < size; i++, j++ ){
      on_vars[j] = vars[i];
      on_weights[j] = w[i];
    }
    on_size++;
  }
  else {
    // Add the values to the off-processor part. This will store
    // the values locally until the finalize call is made. First,
    // check if the current size of the allocated memory is sufficient
    // to store the interpolation. If not, allocate larger arrays.
    if (off_size >= max_off_size){
      // Double the current size of the array and copy the old 
      // values into the newly allocated part.
      max_off_size += max_off_size;
      int *tmp_nums = off_nums;
      int *tmp_rowp = off_rowp;
      off_nums = new int[ max_off_size ];
      off_rowp = new int[ max_off_size+1 ];

      memcpy(off_nums, tmp_nums, off_size*sizeof(int));
      memcpy(off_rowp, tmp_rowp, (off_size+1)*sizeof(int));
      delete [] tmp_nums;
      delete [] tmp_rowp;
    }
    if (off_rowp[off_size] + size > max_off_weights){
      // Increase the size of the working pointer array, and
      // copy over the old values into the new array
      max_off_weights += size + max_off_weights;
      int *tmp_vars = off_vars;
      TacsScalar *tmp_weights = off_weights;
      off_vars = new int[ max_off_weights ];
      off_weights = new TacsScalar[ max_off_weights ];

      memcpy(off_vars, tmp_vars, off_rowp[off_size]*sizeof(int));
      memcpy(off_weights, tmp_weights, off_rowp[off_size]*sizeof(TacsScalar));
      delete [] tmp_vars;
      delete [] tmp_weights;
    }
    
    // Store the values temporarily 
    off_nums[off_size] = num;
    off_rowp[off_size+1] = off_rowp[off_size] + size;
    for ( int i = 0, j = off_rowp[off_size]; i < size; i++, j++ ){
      off_vars[j] = vars[i];
      off_weights[j] = w[i];
    }
    off_size++;
  }
}

/*
  Finalize the interpolation and set up the internal data structures
  so that the object can be used for interpolation/extrapolation.
  This code is collective on all processors in the communicator. 

  This finalize call performs a few critical tasks: 

  1. All interpolation weights are passed to the processors which own
  them.  

  2. The interpolation is divided into two parts: the local part and
  the external part. The local part only acts on local variables,
  while the external part acts on any external variable.

  3. All weights are normalized by the sum of the weights in each row.

  After these operations, the BVecInterp class can be utilized to
  perform restriction or prolongation operations.
*/
void BVecInterp::finalize(){
  // Retrieve the MPI comm size
  int mpiSize;
  MPI_Comm_size(comm, &mpiSize);

  // Find the number of contributions that need to be sent to other
  // processors. Count the number of times each off-processor 
  // equation is referenced. 
  int * tmp_count = new int[ mpiSize ];
  int * tmp_ptr = new int[ mpiSize+1 ];
  int * tmp_weights_count = new int[ mpiSize ];
  int * tmp_weights_ptr = new int[ mpiSize+1 ];

  memset(tmp_count, 0, mpiSize*sizeof(int));
  memset(tmp_weights_count, 0, mpiSize*sizeof(int));

  for ( int i = 0; i < off_size; i++ ){
    int index = FElibrary::findInterval(off_nums[i], 
					outOwnerRange, mpiSize+1);
    tmp_count[index]++;
    tmp_weights_count[index] += off_rowp[i+1] - off_rowp[i];
  }

  // Set a pointer array into the newly allocated block of memory
  // that will store the arranged interpolants
  tmp_ptr[0] = 0;
  tmp_weights_ptr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    tmp_ptr[i+1] = tmp_ptr[i] + tmp_count[i];
    tmp_weights_ptr[i+1] = tmp_weights_ptr[i] + tmp_weights_count[i];
  }

  // Allocate memory for the temporary data storage. Note that we store
  // the number of weights per row, rather than a pointer to data
  // (like CSR format) since this will be easier to transfer between
  // processors since the relative offset won't change. 
  int * tmp_nums = new int[ off_size ];
  int * tmp_weights_per_row = new int[ off_size ];
  int * tmp_vars = new int[ off_rowp[off_size] ];
  TacsScalar * tmp_weights = new TacsScalar[ off_rowp[off_size] ];

  // Go through the entire list of interpolant contributions and 
  // copy over the data to the required off-diagonal spot in the
  // temporary data arrays.
  for ( int i = 0; i < off_size; i++ ){
    int index = FElibrary::findInterval(off_nums[i], 
					outOwnerRange, mpiSize+1);

    if (!(index < 0 || index >= mpiSize)){
      // Copy the values over to a temporary array
      tmp_nums[tmp_ptr[index]] = off_nums[i];
      
      // Copy the weight and variable information to the new array
      int j = tmp_weights_ptr[index];
      for ( int k = off_rowp[i]; k < off_rowp[i+1]; k++, j++ ){
	tmp_weights[j] = off_weights[k];
	tmp_vars[j] = off_vars[k];
      }
      
      // Record the number of weights stored for this rows
      tmp_weights_per_row[tmp_ptr[index]] = off_rowp[i+1] - off_rowp[i];

      // Increment the pointers to where the next data will be added
      tmp_ptr[index]++;
      tmp_weights_ptr[index] += off_rowp[i+1] - off_rowp[i];
    }
  }

  // Reset the pointer arrays so that they again point to the
  // beginning of each block of data to be sent to the other
  // processors
  tmp_ptr[0] = 0;
  tmp_weights_ptr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    tmp_ptr[i+1] = tmp_ptr[i] + tmp_count[i];
    tmp_weights_ptr[i+1] = tmp_weights_ptr[i] + tmp_weights_count[i];
  }

  // Delete the actual values of the off-diagonal contributions - they
  // are no-longer required since we have copied all the information
  // to the temporary arrays.
  delete [] off_nums;
  delete [] off_rowp;
  delete [] off_vars;
  delete [] off_weights;

  // Send the number of out-going interpolation values to the
  // recieving processors.
  int * in_count = new int[ mpiSize ];
  int * in_ptr = new int[ mpiSize+1 ];
  int * in_weights_count = new int[ mpiSize ];
  int * in_weights_ptr = new int[ mpiSize+1 ];

  // Send/recieve the in-coming counts
  MPI_Alltoall(tmp_count, 1, MPI_INT, in_count, 1, MPI_INT, comm);

  // Set a pointer for the incoming num/count data
  in_ptr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    in_ptr[i+1] = in_ptr[i] + in_count[i];
  }

  // Send/recieve the in-coming weight counts
  MPI_Alltoall(tmp_weights_count, 1, MPI_INT, 
	       in_weights_count, 1, MPI_INT, comm);

  // Set a pointer for the incoming weights/variables data
  in_weights_ptr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    in_weights_ptr[i+1] = in_weights_ptr[i] + in_weights_count[i];
  }

  // Allocate the required space for all the incoming data
  int * in_nums = new int[ in_ptr[mpiSize] ];
  int * in_weights_per_row = new int[ in_ptr[mpiSize] ];
  int * in_vars = new int[ in_weights_ptr[mpiSize] ];
  TacsScalar * in_weights = new TacsScalar[ in_weights_ptr[mpiSize] ];

  // Send and recieve all the data destined for this processor
  // Send the variable numbers
  MPI_Alltoallv(tmp_nums, tmp_count, tmp_ptr, MPI_INT,
		in_nums, in_count, in_ptr, MPI_INT, comm);
  
  // Send the number of variables per row
  MPI_Alltoallv(tmp_weights_per_row, tmp_count, tmp_ptr, MPI_INT,
		in_weights_per_row, in_count, in_ptr, MPI_INT, comm);

  // Send the variables for each row
  MPI_Alltoallv(tmp_vars, tmp_weights_count, tmp_weights_ptr, MPI_INT,
		in_vars, in_weights_count, in_weights_ptr, MPI_INT, comm);

  // Send the weights for each variable
  MPI_Alltoallv(tmp_weights, tmp_weights_count, tmp_weights_ptr, TACS_MPI_TYPE,
		in_weights, in_weights_count, in_weights_ptr, 
		TACS_MPI_TYPE, comm);

  // Delete the temporary data - this is no longer required since we
  // now have the final data on the actual processors
  delete [] tmp_count;
  delete [] tmp_ptr;
  delete [] tmp_weights_count;
  delete [] tmp_weights_ptr;
  delete [] tmp_nums;
  delete [] tmp_weights_per_row;
  delete [] tmp_vars;
  delete [] tmp_weights;

  // Delete the pointers that indicated where the data came from since
  // this information is not relevant, but record how many total entries
  // were sent to this processor.
  int in_size = in_ptr[mpiSize];
  delete [] in_count;
  delete [] in_ptr;
  delete [] in_weights_count;
  delete [] in_weights_ptr;

  // Now all the data required to assemble the internal data
  // structures are on the local processors. Now, we just assemble
  // the on and off-processor parts, but all assignments (i.e. all
  // the output) is local.

  // Allocate space for the internal and external portions of the
  // matrix
  rowp = new int[ N+1 ];
  memset(rowp, 0, (N+1)*sizeof(int));

  ext_rowp = new int[ N+1 ];
  memset(ext_rowp, 0, (N+1)*sizeof(int));  

  // Count up the contributions to the CSR data structures from the
  // on-processor data
  for ( int i = 0; i < on_size; i++ ){
    // Compute the on-processor variable number
    int num = on_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
      
      // Count up the number of internal and external variables
      // in this row. Note that local/external variables are based
      // on the input variable range!
      int num_local = 0, num_ext = 0;
      
      for ( int j = on_rowp[i]; j < on_rowp[i+1]; j++ ){
	if (on_vars[j] >= inOwnerRange[mpiRank] &&
	    on_vars[j] < inOwnerRange[mpiRank+1]){
	  num_local++;
	}
	else {
	  num_ext++;
	}
      }
	
      rowp[num+1] += num_local;
      ext_rowp[num+1] += num_ext;
    }
    else {
      // Print an error message. This should never happen since
      // we've gone out of our way to check that this condition
      // should not appear
      fprintf(stderr, "Error, local interpolation variable out of range\n");
    }
  }

  // Count up the contributions to the CSR data structures from the
  // off-processor data now stored in the input arrays
  for ( int i = 0, k = 0; i < in_size; i++ ){
    // Compute the on-processor variable number
    int num = in_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
      
      // Count up the number of internal and external variables
      // in this row. Note that local/external variables are based
      // on the input variable range!
      int num_local = 0, num_ext = 0;
      
      for ( int j = 0; j < in_weights_per_row[i]; j++, k++ ){
	if (in_vars[k] >= inOwnerRange[mpiRank] &&
	    in_vars[k] < inOwnerRange[mpiRank+1]){
	  num_local++;
	}
	else {
	  num_ext++;
	}
      }
	
      rowp[num+1] += num_local;
      ext_rowp[num+1] += num_ext;
    }
    else {
      // Print an error message. This should never happen since
      // we've gone out of our way to check that this condition
      // should not appear
      fprintf(stderr, "Error, local interpolation variable out of range\n");
    
      // Increment the pointer to the input array
      k += in_weights_per_row[i];
    }
  }

  // Increment the rowp pointers to the local and external data so that
  // we have not just the counts, but the offsets into the global variable
  // arrays
  rowp[0] = 0;
  ext_rowp[0] = 0;
  for ( int i = 0; i < N; i++ ){
    rowp[i+1] += rowp[i];
    ext_rowp[i+1] += ext_rowp[i];
  }

  // Now, compute the size of the overall arrays required to store all
  // of this data
  cols = new int[ rowp[N] ];
  ext_cols = new int[ ext_rowp[N] ];

  // Add the contributions to the CSR data structure from the on-processor
  // data. Note that this adjusts the rowp and ext_rowp data. This
  // must be restored afterwards.
  for ( int i = 0; i < on_size; i++ ){
    // Compute the on-processor variable number
    int num = on_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
      
      for ( int j = on_rowp[i]; j < on_rowp[i+1]; j++ ){
	if (on_vars[j] >= inOwnerRange[mpiRank] &&
	    on_vars[j] < inOwnerRange[mpiRank+1]){
	  cols[rowp[num]] = on_vars[j];
	  rowp[num]++;
	}
	else {
	  ext_cols[ext_rowp[num]] = on_vars[j];
	  ext_rowp[num]++;
	}
      }
    }
  }

  // Add the entries into the CSR data structure from the 
  // off-processor data
  for ( int i = 0, k = 0; i < in_size; i++ ){
    // Compute the on-processor variable number
    int num = in_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
            
      for ( int j = 0; j < in_weights_per_row[i]; j++, k++ ){
	if (in_vars[k] >= inOwnerRange[mpiRank] &&
	    in_vars[k] < inOwnerRange[mpiRank+1]){
	  cols[rowp[num]] = in_vars[k];
	  rowp[num]++;
	}
	else {
	  ext_cols[ext_rowp[num]] = in_vars[k];
	  ext_rowp[num]++;
	}
      }
    }
    else {
      // Increment the pointer to the input array
      k += in_weights_per_row[i];
    }
  }

  // Adjust the rowp/ext_rowp array so that they again point to their
  // proper positions
  for ( int i = N; i > 0; i-- ){
    rowp[i] = rowp[i-1];
    ext_rowp[i] = ext_rowp[i-1];
  }
  rowp[0] = 0;
  ext_rowp[0] = 0;

  // Sort and uniquify the CSR data structures so that we don't have
  // duplicate entries
  int nodiag = 0; // Don't remove the diagonal from the matrix
  matutils::SortAndUniquifyCSR(N, rowp, cols, nodiag);
  matutils::SortAndUniquifyCSR(N, ext_rowp, ext_cols, nodiag);

  // Allocate space for the weights. Initialize the weight values
  // to zero
  weights = new TacsScalar[ rowp[N] ];
  memset(weights, 0, rowp[N]*sizeof(TacsScalar));

  ext_weights = new TacsScalar[ ext_rowp[N] ];
  memset(ext_weights, 0, ext_rowp[N]*sizeof(TacsScalar));  

  // Add the weight values themselves to the CSR data structure
  for ( int i = 0; i < on_size; i++ ){
    // Compute the on-processor variable number
    int num = on_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
      
      for ( int j = on_rowp[i]; j < on_rowp[i+1]; j++ ){
	if (on_vars[j] >= inOwnerRange[mpiRank] &&
	    on_vars[j] < inOwnerRange[mpiRank+1]){
	  int size = rowp[num+1] - rowp[num];
	  int * item = (int*)bsearch(&on_vars[j], 
				     &cols[rowp[num]], size,  
				     sizeof(int), FElibrary::comparator);
	  if (item){
	    weights[item - cols] += on_weights[j]; 
	  }
	}
	else {
	  int size = ext_rowp[num+1] - ext_rowp[num];
	  int * item = (int*)bsearch(&on_vars[j], 
				     &ext_cols[ext_rowp[num]], size,  
				     sizeof(int), FElibrary::comparator);
	  if (item){
	    ext_weights[item - ext_cols] += on_weights[j]; 
	  }
	}
      }
    }
  }

  // Add the entries into the CSR data structure from the 
  // off-processor data
  for ( int i = 0, k = 0; i < in_size; i++ ){
    // Compute the on-processor variable number
    int num = in_nums[i];
    
    if (num >= outOwnerRange[mpiRank] &&
	num < outOwnerRange[mpiRank+1]){
      // Adjust the range of the output variable to the local index
      num = num - outOwnerRange[mpiRank];
            
      for ( int j = 0; j < in_weights_per_row[i]; j++, k++ ){
	if (in_vars[k] >= inOwnerRange[mpiRank] &&
	    in_vars[k] < inOwnerRange[mpiRank+1]){
	  int size = rowp[num+1] - rowp[num];
	  int * item = (int*)bsearch(&in_vars[k], 
				     &cols[rowp[num]], size,  
				     sizeof(int), FElibrary::comparator);
	  if (item){
	    weights[item - cols] += in_weights[k]; 
	  }
	}
	else {
	  int size = ext_rowp[num+1] - ext_rowp[num];
	  int * item = (int*)bsearch(&in_vars[k], 
				     &ext_cols[ext_rowp[num]], size,  
				     sizeof(int), FElibrary::comparator);
	  if (item){
	    ext_weights[item - ext_cols] += in_weights[k]; 
	  }
	}
      }
    }
    else {
      // Increment the pointer to the input array
      k += in_weights_per_row[i];
    }
  }

  // Delete the incoming data
  delete [] in_nums;
  delete [] in_weights_per_row;
  delete [] in_vars;
  delete [] in_weights;
  
  // Delete the on-processor data
  delete [] on_nums;
  delete [] on_rowp;
  delete [] on_vars;
  delete [] on_weights;

  // Allocate space for the external variable numbers
  int * ext_vars = new int[ ext_rowp[N] ];
  memcpy(ext_vars, ext_cols, ext_rowp[N]*sizeof(int));
  num_ext_vars = FElibrary::uniqueSort(ext_vars, ext_rowp[N]);

  // Adjust both the internal and external CSR data structures to 
  // reflect the internal ordering. We order the local on-processor
  // components of the input map based on shifting the variable
  // input from inOwnerRange[mpiRank] to start at zero. The external
  // data ordering is based on the ext_vars array.
  for ( int j = 0; j < rowp[N]; j++ ){
    cols[j] = cols[j] - inOwnerRange[mpiRank];
  }

  // Adjust the external ordering
  for ( int j = 0; j < ext_rowp[N]; j++ ){
    int * item = (int*)bsearch(&ext_cols[j], ext_vars, num_ext_vars,
			       sizeof(int), FElibrary::comparator);
    ext_cols[j] = item - ext_vars;
  }

  // ext_rowp/ext_cols/ext_weights now contains the off-diagonal components
  // this object now can distribute between the two variables
  BVecIndices * bindex = new BVecIndices(&ext_vars, num_ext_vars);
  vecDist = new BVecDistribute(inMap, bindex); // vecDist now owns ext_vars
  vecDist->incref();

  // Allocate memroy for the in-coming data
  x_ext = new TacsScalar[ bsize*num_ext_vars ];

  // Set all the pointers to null
  on_nums = NULL;
  on_rowp = NULL;
  on_vars = NULL;
  on_weights = NULL;

  off_nums = NULL;
  off_rowp = NULL;
  off_vars = NULL;
  off_weights = NULL;

  // Normalize the weights across both the internal/external mappings
  for ( int i = 0; i < N; i++ ){
    TacsScalar w = 0.0;

    // Compute the sum of the internal and external weights
    for ( int j = rowp[i]; j < rowp[i+1]; j++ ){
      w += weights[j];
    }
    for ( int j = ext_rowp[i]; j < ext_rowp[i+1]; j++ ){
      w += ext_weights[j];
    }

    // Normalize the weights
    if (w != 0.0){
      for ( int j = rowp[i]; j < rowp[i+1]; j++ ){
        weights[j] /= w;
      }
      for ( int j = ext_rowp[i]; j < ext_rowp[i+1]; j++ ){
	ext_weights[j] /= w;
      }
    }
  }
}

/*
  Perform the interpolation from inVec to outVec:

  Interp*inVec -> outVec

  input:
  inVec:  the input vector

  output:
  outVec: the interpolated output vector
*/
void BVecInterp::mult( BVec * inVec, BVec * outVec ){
  if (!vecDist){
    fprintf(stderr, "[%d] Must call finalize before using BVecInterp \
object\n", mpiRank);
    return;
  }

  // Zero the entries
  outVec->zeroEntries();

  // Get the internal arrays for in/out
  TacsScalar *in, *out;
  inVec->getArray(&in);
  outVec->getArray(&out);

  // Initialize the communication from off-processor components to the
  // on-processor values
  vecDist->beginForward(inVec, x_ext);

  // Multiply the on-processor part
  multadd(bsize, N, rowp, cols, weights, in, out);

  // Finish the off-processo communication
  vecDist->endForward(inVec, x_ext);

  // Multiply the off-processor part
  multadd(bsize, N, ext_rowp, ext_cols, ext_weights, x_ext, out);
}

/*
  Perform the interpolation from inVec to outVec:

  addVec + Interp*inVec -> outVec

  input:
  inVec:  the input vector
  addVec: the vector to add to the output

  output:
  outVec: the interpolated output vector
*/
void BVecInterp::multAdd( BVec * inVec, BVec * addVec, 
			  BVec * outVec ){
  if (!vecDist){
    fprintf(stderr, "[%d] Must call finalize before using BVecInterp \
object\n", mpiRank);
    return;
  }

  // If the two vectors are not the same, copy the values to
  // the outVec
  if (outVec != addVec){
    outVec->copyValues(addVec);
  }

  // Get the internal arrays for in/out
  TacsScalar *in, *out;
  inVec->getArray(&in);
  outVec->getArray(&out);

  // Initialize the communication from off-processor components to the
  // on-processor values
  vecDist->beginForward(inVec, x_ext);

  // Multiply the on-processor part
  multadd(bsize, N, rowp, cols, weights, in, out);

  // Finish the off-processo communication
  vecDist->endForward(inVec, x_ext);

  // Multiply the off-processor part
  multadd(bsize, N, ext_rowp, ext_cols, ext_weights, x_ext, out);
}

/*
  Perform the interpolation from inVec to outVec:

  Interp*inVec -> outVec

  input:
  inVec:  the input vector

  output:
  outVec: the interpolated output vector
*/
void BVecInterp::multTranspose( BVec * inVec, BVec * outVec ){
  if (!vecDist){
    fprintf(stderr, "[%d] Must call finalize before using BVecInterp \
object\n", mpiRank);
    return;
  }

  // Zero the entries of the output array
  outVec->zeroEntries();

  // Get the local arrays
  TacsScalar *in, *out;
  inVec->getArray(&in);
  outVec->getArray(&out);

  // Zero the off-processor contribution
  memset(x_ext, 0, bsize*num_ext_vars*sizeof(TacsScalar));

  // Multiply the off-processor part first
  multtransadd(bsize, N, ext_rowp, ext_cols, ext_weights, in, x_ext);

  // Initialize communication to the off-processor part
  vecDist->beginReverse(x_ext, outVec, BVecDistribute::ADD);

  // Multiply the on-processor part
  multtransadd(bsize, N, rowp, cols, weights, in, out);
  
  // Finalize the communication to the off-processor part
  vecDist->endReverse(x_ext, outVec, BVecDistribute::ADD);
}

/*
  Perform the interpolation from inVec to outVec:

  addVec + Interp*inVec -> outVec

  input:
  inVec:  the input vector
  addVec: the vector to add to the output

  output:
  outVec: the interpolated output vector
*/
void BVecInterp::multTransposeAdd( BVec * inVec, BVec * addVec, 
				   BVec * outVec ){
  if (!vecDist){
    fprintf(stderr, "[%d] Must call finalize before using BVecInterp \
object\n", mpiRank);
    return;
  }

  // If the add and output vectors are different, copy the values
  // over to the new entry
  if (outVec != addVec){
    outVec->copyValues(addVec);
  }

  // Get the local arrays
  TacsScalar *in, *out;
  inVec->getArray(&in);
  outVec->getArray(&out);

  // Zero the off-processor contribution
  memset(x_ext, 0, bsize*num_ext_vars*sizeof(TacsScalar));

  // Multiply the off-processor part first
  multtransadd(bsize, N, ext_rowp, ext_cols, ext_weights, in, x_ext);

  // Initialize communication to the off-processor part
  vecDist->beginReverse(x_ext, outVec, BVecDistribute::ADD);

  // Multiply the on-processor part
  multtransadd(bsize, N, rowp, cols, weights, in, out);
  
  // Finalize the communication to the off-processor part
  vecDist->endReverse(x_ext, outVec, BVecDistribute::ADD);
}

/*
  Print the weights to the specified file name
*/
void BVecInterp::printInterp( const char * filename ){
  FILE * fp = fopen(filename, "w");
  if (fp){
    fprintf(fp, "BVecInterp \n");
    for ( int i = 0; i < N; i++ ){
      fprintf(fp, "Row: %d\n", i);
      
      for ( int j = rowp[i]; j < rowp[i+1]; j++ ){
        if (fabs(weights[j]) > 1e-12){
          fprintf(fp, "(%d,%f) ", cols[j], RealPart(weights[j]));
        }
      }
      fprintf(fp, "\n");
    }
    fclose(fp);
  }
}

/*
  The following are the block-specific and generic code for the
  matrix-vector multiplications required within the BVecInterp class.
  The idea is that these will run faster than a generic implementation
  of the matrix-vector multiplication. These will be most important
  for very large meshes where the number of interpolations requried
  (say within a multigrid algorithm) is considerably higher.
*/

/*
  Compute a matrix-vector product for generic bsize
*/
void BVecInterpMultAddGen( int bsize, int nrows, 
			   const int * rowp, const int * cols,
			   const TacsScalar * w,
			   const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];
    
    for (; j < end; j++ ){
      for ( int k = 0; k < bsize; k++ ){
	y[bsize*i+k] += w[0]*x[bsize*cols[j]+k];
      }
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for generic bsize
*/
void BVecInterpMultTransposeAddGen( int bsize, int nrows, 
				    const int * rowp, const int * cols,
				    const TacsScalar * w,
				    const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      for ( int k = 0; k < bsize; k++ ){
	y[bsize*cols[j]+k] += w[0]*x[bsize*i+k];
      }
      w++;
    }
  }
}

/*
  Compute a matrix-vector product for bsize = 1
*/
void BVecInterpMultAdd1( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * w,
			 const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];
    
    for (; j < end; j++ ){
      y[i] += w[0]*x[cols[j]];
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for bsize = 1
*/
void BVecInterpMultTransposeAdd1( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * w,
				  const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[cols[j]] += w[0]*x[i];
      w++;
    }
  }
}

/*
  Compute a matrix-vector product for bsize = 2
*/
void BVecInterpMultAdd2( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * w,
			 const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[2*i]   += w[0]*x[2*cols[j]];
      y[2*i+1] += w[0]*x[2*cols[j]+1];
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for bsize = 2
*/
void BVecInterpMultTransposeAdd2( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * w,
				  const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[2*cols[j]]   += w[0]*x[2*i];
      y[2*cols[j]+1] += w[0]*x[2*i+1];
      w++;
    }
  }
}

/*
  Compute a matrix-vector product for bsize = 3
*/
void BVecInterpMultAdd3( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * w,
			 const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];
    
    for (; j < end; j++ ){
      y[3*i]   += w[0]*x[3*cols[j]];
      y[3*i+1] += w[0]*x[3*cols[j]+1];
      y[3*i+2] += w[0]*x[3*cols[j]+2];
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for bsize = 3
*/
void BVecInterpMultTransposeAdd3( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * w,
				  const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[3*cols[j]]   += w[0]*x[3*i];
      y[3*cols[j]+1] += w[0]*x[3*i+1];
      y[3*cols[j]+2] += w[0]*x[3*i+2];
      w++;
    }
  }
}

/*
  Compute a matrix-vector product for bsize = 5
*/
void BVecInterpMultAdd5( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * w,
			 const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];
    
    for (; j < end; j++ ){
      y[5*i]   += w[0]*x[5*cols[j]];
      y[5*i+1] += w[0]*x[5*cols[j]+1];
      y[5*i+2] += w[0]*x[5*cols[j]+2];
      y[5*i+3] += w[0]*x[5*cols[j]+3];
      y[5*i+4] += w[0]*x[5*cols[j]+4];
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for bsize = 5
*/
void BVecInterpMultTransposeAdd5( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * w,
				  const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[5*cols[j]]   += w[0]*x[5*i];
      y[5*cols[j]+1] += w[0]*x[5*i+1];
      y[5*cols[j]+2] += w[0]*x[5*i+2];
      y[5*cols[j]+3] += w[0]*x[5*i+3];
      y[5*cols[j]+4] += w[0]*x[5*i+4];
      w++;
    }
  }
}

/*
  Compute a matrix-vector product for bsize = 5
*/
void BVecInterpMultAdd6( int bsize, int nrows, 
			 const int * rowp, const int * cols,
			 const TacsScalar * w,
			 const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];
    
    for (; j < end; j++ ){
      y[6*i]   += w[0]*x[6*cols[j]];
      y[6*i+1] += w[0]*x[6*cols[j]+1];
      y[6*i+2] += w[0]*x[6*cols[j]+2];
      y[6*i+3] += w[0]*x[6*cols[j]+3];
      y[6*i+4] += w[0]*x[6*cols[j]+4];
      y[6*i+5] += w[0]*x[6*cols[j]+5];
      w++;
    }
  }
}

/*
  Compute the matrix-vector transpose product for bsize = 6
*/
void BVecInterpMultTransposeAdd6( int bsize, int nrows, 
				  const int * rowp, const int * cols,
				  const TacsScalar * w,
				  const TacsScalar * x, TacsScalar * y ){
  for ( int i = 0; i < nrows; i++ ){
    int j = rowp[i];
    int end = rowp[i+1];

    for (; j < end; j++ ){
      y[6*cols[j]]   += w[0]*x[6*i];
      y[6*cols[j]+1] += w[0]*x[6*i+1];
      y[6*cols[j]+2] += w[0]*x[6*i+2];
      y[6*cols[j]+3] += w[0]*x[6*i+3];
      y[6*cols[j]+4] += w[0]*x[6*i+4];
      y[6*cols[j]+5] += w[0]*x[6*i+5];
      w++;
    }
  }
}