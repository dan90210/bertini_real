#include "solver_nullspace_left.hpp"




//the main wrapper function for chaining into the nullspacejac solver.
// pass in:
//     •
//
// 
void nullspace_config::clear()
{
	
	free(randomized_degrees); // the degrees of the randomized functions (not derivatives)
	
	for (int ii=0; ii<ambient_dim-target_dim; ii++) {
		for (int jj=0; jj<max_degree; jj++) {
			clear_vec_mp(starting_linears[ii][jj]);
		}
		free(starting_linears[ii]);
	}
	free(starting_linears);
	
	for (int ii=0; ii<num_additional_linears; ii++){
		clear_vec_mp(additional_linears_starting[ii]);
		clear_vec_mp(additional_linears_terminal[ii]);
	}
	free(additional_linears_terminal);
	free(additional_linears_starting);
	

	for (int ii=0; ii<num_v_linears; ii++) 
		clear_vec_mp(v_linears[ii]);
	free(v_linears);
	
	clear_vec_mp(v_patch);
	
	for (int ii=0; ii<target_crit_codim; ii++) 
		clear_vec_mp(target_projection[ii]);
	free(target_projection);
	
	clear_mat_mp(randomizer_matrix);
	clear_mat_mp(post_randomizer_matrix);
}




void nullspace_config::print()
{
	std::stringstream converter;
	
	std::cout << "*******************\n\tns_config:\n\n";

	std::cout << "#x: " << num_x_vars << "\n#v: " << num_v_vars << std::endl;
	
	std::cout << "# additional linears: " << this->num_additional_linears << std::endl;
	std::cout << "# jacobian equations: " << this->num_jac_equations << std::endl;
	std::cout << "# randomized equations: " << this->num_randomized_eqns << std::endl;
	std::cout << "max_degree:\t" << max_degree << std::endl;
	
	print_matrix_to_screen_matlab(this->randomizer_matrix,"R");
	print_matrix_to_screen_matlab(this->post_randomizer_matrix,"S");
	
	
	for (int ii=0; ii<num_additional_linears; ii++) {
		converter << "additional_linear_starting_" << ii+1;
		print_point_to_screen_matlab(additional_linears_starting[ii],converter.str());
		converter.str("");
	}
	
	
	for (int ii=0; ii<num_additional_linears; ii++) {
		converter << "additional_linear_terminal_" << ii+1;
		print_point_to_screen_matlab(additional_linears_terminal[ii],converter.str());
		converter.str("");
	}
	
	
	
	for (int ii=0; ii<num_jac_equations; ii++) {
		for (int jj=0; jj<this->max_degree; jj++) {
			converter << "starting_linear_" << ii+1 << "_" << jj+1;
			print_point_to_screen_matlab(starting_linears[ii][jj],converter.str());
			converter.str("");
		}
	}
	
	for (int ii=0; ii<num_jac_equations; ii++) {
		converter << "v_linear_" << ii+1;
			print_point_to_screen_matlab(v_linears[ii],converter.str());
		converter.str("");
	}
	
	std::cout << "\n********************\n" << std::endl;
	
}



int nullspacejac_eval_data_mp::send(int target_id, parallelism_config & mpi_config)
{
	// need to send the data to a worker
//	MPI_Send(MPICH2_CONST void*, int, MPI_Datatype, int, int, MPI_Comm) MPICH_ATTR_POINTER_WITH_TYPE_TAG(1,3);
	int solver_choice = NULLSPACE;
	MPI_Send(&solver_choice, 1, MPI_INT, target_id, TYPE_CONFIRMATION, mpi_config.my_communicator);
	
	return SUCCESSFUL;
}

int nullspacejac_eval_data_mp::receive(parallelism_config & mpi_config)
{
	MPI_Status status;
	int confirmation_buffer;
	MPI_Recv(&confirmation_buffer, 1, MPI_INT, 0, MPI_ANY_TAG,
					 MPI_COMM_WORLD, &status);
	
	if (confirmation_buffer != NULLSPACE) {
		mpi_config.abort(777);
	}
	
	return SUCCESSFUL;
}



int nullspacejac_solver_main(int										MPType,
														 witness_set						&W, // carries with it the start points, and the linears.
														 witness_set						*W_new, // new data goes in here
														 nullspace_config				*ns_config,
														 solver_configuration		*solve_options)
{
	
	W_new->num_variables = W.num_variables;
	W_new->num_synth_vars = W.num_synth_vars;
	
	if (MPType==1){
		nullspacejac_solver_mp(MPType,W,W_new,
													 ns_config,solve_options);
	}
	else{
		nullspacejac_solver_d(MPType,W,W_new,
													ns_config,solve_options);
	}
	
	
	W_new->MPType = MPType;
	if (solve_options->complete_witness_set==1){

		//  do something with the linears here?
		cp_patches(W_new,W); // copy the patches over from the original witness set
		cp_names(W_new,W);
	}
	
	
	return 0;
}





int nullspacejac_solver_d(int MPType,
													witness_set & W, 
													witness_set *W_new,
													nullspace_config				*ns_config,
													solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES:                                                        *
 \***************************************************************/
{
	double parse_time = 0;
  FILE *OUT = NULL, *FAIL = safe_fopen_write("failed_paths"), *midOUT = NULL, *rawOUT = safe_fopen_write("raw_data");
  tracker_config_t T;
  prog_t dummyProg;
  bclock_t time1, time2;
  int num_variables = 0, num_crossings = 0, num_sols = 0;
	
	
	
  int *startSub = NULL, *endSub = NULL, *startFunc = NULL, *endFunc = NULL, *startJvsub = NULL, *endJvsub = NULL, *startJv = NULL, *endJv = NULL, **subFuncsBelow = NULL;
  int (*ptr_to_eval_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *) = NULL;
  int (*ptr_to_eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *) = NULL;
	
  nullspacejac_eval_data_d ED;
  trackingStats trackCount;
  double track_time;
	
  bclock(&time1); // initialize the clock.
  init_trackingStats(&trackCount); // initialize trackCount to all 0
	
	//necessary for later whatnot
	int userHom = 0, useRegen = 0, pathMod = 0, paramHom = 0;
	
	cp_tracker_config_t(&T, &solve_options->T);
	

	
	
	//  // call the setup function
	// setup for standard tracking - 'useRegen' is used to determine whether or not to setup 'start'
	num_variables = nullspacejac_setup_d(&OUT, "output",
																			 &midOUT, "midpath_data",
																			 &T, &ED,
																			 &dummyProg,  //arg 7
																			 &startSub, &endSub, &startFunc, &endFunc,
																			 &startJvsub, &endJvsub, &startJv, &endJv, &subFuncsBelow,
																			 &ptr_to_eval_d, &ptr_to_eval_mp,  //args 17,18
																			 "preproc_data", "deg.out",
																			 !useRegen, "nonhom_start", "start",
																			 W,
																			 ns_config,
																			 solve_options);
  
	
	int (*change_prec)(void const *, int) = &change_nullspacejac_eval_prec;
	int (*dehom)(point_d, point_mp, int *, point_d, point_mp, int, void const *, void const *) = &nullspacejac_dehom;
	
	
	
  // error checking
  if (userHom <= 0 && paramHom != 2)
  { // no pathvariables or parameters allowed!
    if (dummyProg.numPathVars > 0)
    { // path variable present
      printf("ERROR: Bertini does not expect path variables when user-defined homotopies are not being used!\n");
      bexit(ERROR_INPUT_SYSTEM);
    }
    if (dummyProg.numPars > 0)
    { // parameter present
      printf("ERROR: Bertini does not expect parameters when user-defined homotopies are not being used!\n");
      bexit(ERROR_INPUT_SYSTEM);
    }
  }
	
  if (T.MPType == 2)  //If we are doing adaptive precision path-tracking, we must set up AMP_eps, AMP_Phi, AMP_Psi based on config settings.
  {
    T.AMP_eps = (double) num_variables * num_variables;  //According to Demmel (as in the AMP paper), n^2 is a very reasonable bound for \epsilon.
    T.AMP_Phi = T.AMP_bound_on_degree*(T.AMP_bound_on_degree-1.0)*T.AMP_bound_on_abs_vals_of_coeffs;  //Phi from the AMP paper.
    T.AMP_Psi = T.AMP_bound_on_degree*T.AMP_bound_on_abs_vals_of_coeffs;  //Psi from the AMP paper.
    // initialize latest_newton_residual_mp to the maximum precision
    mpf_init2(T.latest_newton_residual_mp, T.AMP_max_prec);
  }
	
	
	post_process_t *endPoints = (post_process_t *)bmalloc(W.num_pts * sizeof(post_process_t)); //overallocate, expecting full number of solutions.
	
	
	
	
	if (T.endgameNumber == 3)
	{ // use the track-back endgame
		//        zero_dim_trackBack_d(&trackCount, OUT, rawOUT, midOUT, StartPts, FAIL, pathMod, &T, &ED, ED.BED_mp, ptr_to_eval_d, ptr_to_eval_mp, change_basic_eval_prec, zero_dim_dehom);
		printf("bertini_real not equipped to deal with endgameNumber 3\nexiting\n");
		exit(-99);
	}
	else
	{ // use regular endgame
		nullspacejac_track_d(&trackCount, OUT, rawOUT, midOUT,
												 W,  // was the startpts file pointer.
												 endPoints,
												 FAIL, pathMod,
												 &T, &ED, ED.BED_mp,
												 ptr_to_eval_d, ptr_to_eval_mp,
												 change_prec, dehom,
												 solve_options);
	}
	
	
	fclose(midOUT);
	
	
	// finish the output to rawOUT
	fprintf(rawOUT, "%d\n\n", -1);  // bottom of rawOUT
	
	
	// check for path crossings
	if (solve_options->use_midpoint_checker==1) {
		midpoint_checker(trackCount.numPoints, num_variables,solve_options->midpoint_tol, &num_crossings);
	}
	// setup num_sols
	num_sols = trackCount.successes;
	
  // we report how we did with all paths:
  bclock(&time2);
  totalTime(&track_time, time1, time2);
  if (solve_options->verbose_level>=1)
  {
		printf("nullspace_left report:\n");
    printf("Number of failures:  %d\n", trackCount.failures);
    printf("Number of successes:  %d\n", trackCount.successes);
    printf("Number of paths:  %d\n", trackCount.numPoints);
    printf("Parse Time = %fs\n", parse_time);
    printf("Track Time = %fs\n", track_time);
  }
  fprintf(OUT, "Number of failures:  %d\n", trackCount.failures);
  fprintf(OUT, "Number of successes:  %d\n", trackCount.successes);
  fprintf(OUT, "Number of paths:  %d\n", trackCount.numPoints);
  fprintf(OUT, "Parse Time = %fs\n", parse_time);
  fprintf(OUT, "Track Time = %fs\n", track_time);
	
	
  fclose(OUT);
  fclose(rawOUT);
  fprintf(FAIL, "\n");
  fclose(FAIL);
	
	
	if (num_crossings>0) {
		printf("there were %d path crossings in nullspacejac, according to midpoint checker\n",num_crossings);
		mypause();
	}
	
	BRpostProcessing(endPoints, W_new, trackCount.successes, &ED.preProcData, &T, solve_options);

	
	//DAB is there other stuff which should be cleared here?
	
	free(startSub);
	free(endSub);
	free(startFunc);
	free(endFunc);
	free(startJvsub);
	free(endJvsub);
	free(startJv);
	free(endJv);
	
	
	
  nullspacejac_eval_clear_d(&ED, userHom, T.MPType);
  tracker_config_clear(&T);
	
  return 0;
}





void nullspacejac_track_d(trackingStats *trackCount,
													FILE *OUT, FILE *RAWOUT, FILE *MIDOUT,
													witness_set & W,
													post_process_t *endPoints,  // for holding the produced data.
													FILE *FAIL,
													int pathMod, tracker_config_t *T,
													nullspacejac_eval_data_d *ED_d,
													nullspacejac_eval_data_mp *ED_mp,
													int (*eval_func_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *),
													int (*eval_func_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *),
													int (*change_prec)(void const *, int),
													int (*find_dehom)(point_d, point_mp, int *, point_d, point_mp, int, void const *, void const *),
													solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: does standard zero dimensional tracking                *
 *  in either double precision or adaptive precision             *
 \***************************************************************/
{
	
  int ii, oid, startPointIndex, max = max_threads();
  tracker_config_t *T_copy = NULL;
  nullspacejac_eval_data_d *BED_copy = NULL;
  trackingStats *trackCount_copy = NULL;
  FILE **OUT_copy = NULL, **MIDOUT_copy = NULL, **RAWOUT_copy = NULL, **FAIL_copy = NULL, *NONSOLN = NULL, **NONSOLN_copy = NULL;
	
  // top of RAWOUT - number of variables and that we are doing zero dimensional
  fprintf(RAWOUT, "%d\n%d\n", T->numVars, 0);
	
	
	
	
	
	int (*curr_eval_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *) = NULL;
  int (*curr_eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *) = NULL;
	
	curr_eval_d = &nullspacejac_eval_d;   
  curr_eval_mp = &nullspacejac_eval_mp;
	
	
	
	point_data_d *startPts = NULL;
	generic_set_start_pts(&startPts, W);
	T->endgameOnly = 0;
	
	
  // setup the rest of the structures
	endgame_data_t *EG = NULL; //this will hold the temp solution data produced for each individual track
  setup_nullspacejac_omp_d(max,
													 &EG, &trackCount_copy, trackCount,
													 &OUT_copy, OUT, &RAWOUT_copy, RAWOUT, &MIDOUT_copy, MIDOUT, &FAIL_copy, FAIL, &NONSOLN_copy, NONSOLN,
													 &T_copy, T,
													 &BED_copy, ED_d, ED_mp);
	
	
	
	
	
	
	
	trackCount->numPoints = W.num_pts;
	int solution_counter = 0;
	
	
	
	// track each of the start points
#ifdef _OPENMP
#pragma omp parallel for private(ii, oid, startPointIndex) schedule(runtime)
#endif
	for (ii = 0; ii < W.num_pts; ii++)
	{ // get current thread number
		oid = thread_num();
		if (solve_options->verbose_level>=0)
			printf("nullspacejac tracking path %d of %d\n",ii,W.num_pts);
		
		startPointIndex = ii;
		
//		// print the header of the path to OUT
//		printPathHeader_d(OUT_copy[oid], &startPts[startPointIndex], &T_copy[oid], ii, &BED_copy[oid], eval_func_d);
		
#ifdef printpathnullspace_left
		BED_copy[oid].num_steps = 0;
#endif
		
		if (!check_isstart_nullspacejac_d(startPts[ii].point,
																			&T_copy[oid],
																			&BED_copy[oid]))
		{
			std::cout << "trying to start from a non-start-point\n";
			mypause();
		}
		
		if (T->MPType==2) {
			ED_d->BED_mp->curr_prec = 64;
		}
		
		
		// track the path
		generic_track_path_d(solution_counter, &EG[oid], &startPts[startPointIndex],
															OUT_copy[oid], MIDOUT_copy[oid],
															&T_copy[oid], &BED_copy[oid], BED_copy[oid].BED_mp,
															curr_eval_d, curr_eval_mp, change_prec, find_dehom);
		
#ifdef printpathnullspace_left
		fprintf(BED_copy[oid].FOUT,"-100 %d ",BED_copy[oid].num_steps);
		for (mm=0; mm<BED_copy[oid].num_variables-1; ++mm) {
			fprintf(BED_copy[oid].FOUT,"0 0 ");
		}
		fprintf(BED_copy[oid].FOUT,"\n%d\n\n",EG->retVal);
#endif
		
		
		// check to see if it should be sharpened
		if (EG[oid].retVal == 0 && T_copy[oid].sharpenDigits > 0)
		{ // use the sharpener for after an endgame
			sharpen_endpoint_endgame(&EG[oid], &T_copy[oid], OUT_copy[oid], &BED_copy[oid], BED_copy[oid].BED_mp, curr_eval_d, curr_eval_mp, change_prec);
		}
		
		
		
		int issoln;
		if (EG->prec<64){
			issoln = check_issoln_nullspacejac_d(&EG[oid],  &T_copy[oid], &BED_copy[oid]); }
		else {
			issoln = check_issoln_nullspacejac_mp(&EG[oid], &T_copy[oid], BED_copy[oid].BED_mp); }
		
		
		//get the terminal time in double form
		comp_d time_to_compare;
		if (EG->prec < 64) {
			set_d(time_to_compare,EG->PD_d.time);}
		else {
			mp_to_d(time_to_compare, EG->PD_mp.time); }
		
		
		if ((EG->retVal != 0 && time_to_compare->r > T->minTrackT) || !issoln) {  // <-- this is the real indicator of failure...
			
			trackCount->failures++;
		
			printf("\nthere was a path failure nullspace_left tracking witness point %d\nretVal = %d; issoln = %d\n",ii,EG->retVal, issoln);
			
			print_path_retVal_message(EG->retVal);
			
			if (solve_options->verbose_level > 0) {
				if (EG->prec < 64)
					print_point_to_screen_matlab(EG->PD_d.point,"bad_terminal_point");
				else
					print_point_to_screen_matlab(EG->PD_mp.point,"bad_terminal_point");
			}
			
		}
		else
		{
			//otherwise converged, but may have still had non-zero retval due to other reasons.
			endgamedata_to_endpoint(&endPoints[solution_counter], EG);
			trackCount->successes++;
			solution_counter++; // probably this could be eliminated
		}
		
	}// re: for (ii=0; ii<W.num_pts ;ii++)
	
	
	
	//clear the data structures.
  for (ii = 0; ii >W.num_pts; ii++)
  { // clear startPts[ii]
    clear_point_data_d(&startPts[ii]);
  }
  free(startPts);
	
  clear_nullspacejac_omp_d(max, &EG, &trackCount_copy, trackCount, &OUT_copy, OUT, &RAWOUT_copy, RAWOUT, &MIDOUT_copy, MIDOUT, &FAIL_copy, FAIL, &NONSOLN_copy, NONSOLN, &T_copy, &BED_copy);
	
	
	
  return;
}












//this derived from basic_eval_d
int nullspacejac_eval_d(point_d funcVals, point_d parVals, vec_d parDer, mat_d Jv, mat_d Jp, point_d current_variable_values, comp_d pathVars, void const *ED)
{ // evaluates a special homotopy type, built for bertini_real

	
//	std::cout << "++++++++++++++++++++++++++\n";
	
	
  nullspacejac_eval_data_d *BED = (nullspacejac_eval_data_d *)ED; // to avoid having to cast every time
	
	
	
  int ii, jj, kk, mm;
	int offset;
  comp_d one_minus_s, gamma_s;
	
	set_one_d(one_minus_s);
  sub_d(one_minus_s, one_minus_s, pathVars);  // one_minus_s = (1 - s)
  mul_d(gamma_s, BED->gamma, pathVars);       // gamma_s = gamma * s
	

	vec_d curr_x_vars; init_vec_d(curr_x_vars, BED->num_x_vars);
	curr_x_vars->size = BED->num_x_vars;
	for (ii=0; ii<BED->num_x_vars; ii++) 
		set_d(&curr_x_vars->coord[ii], &current_variable_values->coord[ii]);
	
	vec_d curr_v_vars; init_vec_d(curr_v_vars, BED->num_v_vars);
	curr_v_vars->size = BED->num_v_vars;
	for (ii=0; ii<BED->num_v_vars; ii++)
		set_d(&curr_v_vars->coord[ii], &current_variable_values->coord[ii+BED->num_x_vars]);
	
	vec_d patchValues; init_vec_d(patchValues, 0);
	vec_d temp_function_values; init_vec_d(temp_function_values,0);
	

	vec_d AtimesF;  init_vec_d(AtimesF,0);
	vec_d linprod_x;  init_vec_d(linprod_x, BED->num_jac_equations);
		linprod_x->size = BED->num_jac_equations;
	vec_d linprod_times_gamma_s; init_vec_d(linprod_times_gamma_s,BED->num_jac_equations);
		linprod_times_gamma_s->size = BED->num_jac_equations;
	vec_d tempvec; init_vec_d(tempvec,0);
	vec_d tempvec2; init_vec_d(tempvec2,0);
	
	
	
	mat_d Jv_Patch; init_mat_d(Jv_Patch, 0, 0);
	mat_d tempmat; init_mat_d(tempmat,BED->num_variables-1,BED->num_variables-1);
		tempmat->rows = tempmat->cols = BED->num_variables-1; // change the size indicators

	mat_d lin_func_vals; init_mat_d(lin_func_vals,BED->num_jac_equations, BED->max_degree);
		lin_func_vals->rows = BED->num_jac_equations; lin_func_vals->cols = BED->max_degree;
	
	
	
	
	
	mat_d AtimesJ; init_mat_d(AtimesJ,1,1);
	mat_d Jv_jac; init_mat_d(Jv_jac,0,0);
	mat_d temp_jacobian_functions, temp_jacobian_parameters;
		init_mat_d(temp_jacobian_functions,0,0); init_mat_d(temp_jacobian_parameters,0,0);
	
	mat_d linprod_derivative_wrt_x;
		init_mat_d(linprod_derivative_wrt_x, BED->num_jac_equations, BED->num_x_vars);
		linprod_derivative_wrt_x->rows = BED->num_jac_equations; linprod_derivative_wrt_x->cols = BED->num_x_vars;
	
	
	comp_d running_prod;
	comp_d temp, temp2, temp3;
	
	
	
	mat_d S_times_Jf_pi;  init_mat_d(S_times_Jf_pi, BED->post_randomizer_matrix->rows, BED->num_v_vars); // set up temp matrix
		S_times_Jf_pi->rows = BED->post_randomizer_matrix->rows; S_times_Jf_pi->cols = BED->num_v_vars;
	vec_d target_function_values;  init_vec_d(target_function_values,0);
	vec_d target_function_values_times_oneminus_s;  init_vec_d(target_function_values_times_oneminus_s,0);
	
	vec_d start_function_values;
	init_vec_d(start_function_values,BED->num_jac_equations); start_function_values->size = BED->num_jac_equations;
	
	//initialize some containers, for the unused stuff from the called evaluators.
	point_d unused_function_values, unused_parVals;
		init_vec_d(unused_function_values,0);init_vec_d(unused_parVals,0);
	vec_d unused_parDer; init_vec_d(unused_parDer,0);
	mat_d unused_Jp; init_mat_d(unused_Jp,0,0);
	mat_d perturbed_Jv; init_mat_d(perturbed_Jv,0,0);
	
	
	mat_d perturbed_AtimesJ, tempmat3; // create matrices
		init_mat_d(perturbed_AtimesJ,0,0); init_mat_d(tempmat3,0,0);
	
	
	mat_d tempmat1,tempmat2; // create temp matrices
		init_mat_d(tempmat1,BED->num_jac_equations,BED->num_v_vars);
		tempmat1->rows = BED->num_jac_equations; tempmat1->cols = BED->num_v_vars;
	
		init_mat_d(tempmat2,BED->num_jac_equations,BED->num_v_vars);
		tempmat2->rows = BED->num_jac_equations; tempmat2->cols = BED->num_v_vars;
	
	mat_d jac_homogenizing_matrix; init_mat_d(jac_homogenizing_matrix,0,0);
	
	
	//initialize the jacobians we will work with.
	point_d perturbed_forward_variables, perturbed_backward_variables;
	init_vec_d(perturbed_forward_variables,0); init_vec_d(perturbed_backward_variables,0);
	change_size_vec_d(perturbed_forward_variables, BED->num_x_vars);
	change_size_vec_d(perturbed_backward_variables,BED->num_x_vars);
	perturbed_backward_variables->size = perturbed_forward_variables->size = BED->num_x_vars;
	
	
	// the main evaluations for $x$
	evalProg_d(temp_function_values, parVals, parDer, temp_jacobian_functions, temp_jacobian_parameters, curr_x_vars, pathVars, BED->SLP);
	
	
  // evaluate the patch
  patch_eval_d(    patchValues, parVals, parDer, Jv_Patch, Jp, curr_x_vars, pathVars, &BED->patch);  // Jp is ignored
	

	
	//resize output variables to correct size
	change_size_vec_d(funcVals,BED->num_variables);
  change_size_mat_d(Jv, BED->num_variables, BED->num_variables);
  change_size_mat_d(Jp, BED->num_variables, 1);
	
	
	//////
	// initialize stuff to all 0's
	///////
	
  funcVals->size = Jv->rows = Jp->rows = BED->num_variables;
  Jv->cols = BED->num_variables;  //  <-- this must be square
  Jp->cols = 1;
	
	for (ii=0; ii<Jv->rows; ii++) 
		for (jj=0; jj<Jv->cols; jj++) 
			set_zero_d(&Jv->entry[ii][jj]);
	
	for (ii = 0; ii<BED->num_variables; ii++) 
		set_zero_d(&Jp->entry[ii][0]);  // initialize entire matrix to 0
	
	
	// orig eqns
	
	
	// randomize
	mul_mat_vec_d(AtimesF,BED->randomizer_matrix, temp_function_values); // set values of AtimesF (A is randomization matrix)

	// set func vals
	for (ii=0; ii<AtimesF->size; ii++)  // for each function, after (real) randomization
		set_d(&funcVals->coord[ii], &AtimesF->coord[ii]);
	
	
	

	// the jacobian equations for orig
	
	//  randomize the original functions and jacobian
	mat_mul_d(AtimesJ,BED->randomizer_matrix,temp_jacobian_functions);
	
	// copy the jacobian into the return value for the evaluator
	for (ii=0; ii< AtimesJ->rows; ii++) 
		for (jj=0; jj< AtimesJ->cols; jj++)
			set_d(&Jv->entry[ii][jj],&AtimesJ->entry[ii][jj]); 

	// copy in the transpose of the (randomized) jacobian, omitting the homogenizing variable
	for (ii=0; ii< AtimesJ->rows; ii++)
		for (jj=1; jj<BED->num_x_vars; jj++)
			set_d(&BED->jac_with_proj->entry[jj-1][ii], &AtimesJ->entry[ii][jj]);
	//√
	
	
	// the additional linears.  there are $r-\ell$ of them.
	offset = BED->num_randomized_eqns;
	for (ii=0; ii< BED->num_additional_linears; ii++) {
		dot_product_d(temp, BED->additional_linears_terminal[ii], curr_x_vars);
		mul_d(temp3, temp, one_minus_s);
		neg_d(&Jp->entry[offset+ii][0], temp);  // Jp = -terminal
		
		dot_product_d(temp,  BED->additional_linears_starting[ii], curr_x_vars);
		
		mul_d(temp2, temp, BED->gamma);
		add_d(&Jp->entry[offset+ii][0], &Jp->entry[offset+ii][0], temp2);   // Jp = -terminal + gamma*start
		
		mul_d(temp2, temp, gamma_s);
		
		add_d(&funcVals->coord[offset+ii],temp2, temp3); // (gamma*s)*start(x) + (1-s)*terminal(x)
		
		for (jj=0; jj<BED->num_x_vars; jj++) {
			mul_d(temp, gamma_s,    &BED->additional_linears_starting[ii]->coord[jj]); 
			mul_d(temp2,one_minus_s,&BED->additional_linears_terminal[ii]->coord[jj]);
			
			add_d(&Jv->entry[ii+offset][jj], temp, temp2);
		}
	} // √
	
	
	
	// NOW WE WILL WORK ON THE TARGET SYSTEM'S FUNCTION VALUES
	
	
	
	// make the homogenizing matrix for the $x$ variables
	make_matrix_ID_d(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
	
	for (ii=0; ii<BED->num_randomized_eqns; ii++)
		for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[ii]-1)); jj++)
			mul_d(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
	
	for (ii=BED->num_randomized_eqns; ii<BED->num_v_vars; ii++)
		for (jj=0; jj<(BED->max_degree); jj++) // these are all degree 1
			mul_d(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
	

	mat_mul_d(tempmat, BED->post_randomizer_matrix, BED->jac_with_proj); // jac with proj having been set above with unperturbed values
	mat_mul_d(S_times_Jf_pi, tempmat, jac_homogenizing_matrix); // jac with proj having been set above with unperturbed values


	mul_mat_vec_d(target_function_values, S_times_Jf_pi, curr_v_vars);
	vec_mulcomp_d(target_function_values_times_oneminus_s, target_function_values, one_minus_s);
	
	
	
	//  THE LINPROD START SYSTEM FUNCTION VALUES
	offset = BED->num_randomized_eqns + BED->num_additional_linears;  
	// the product of the linears
	for (jj=0; jj<BED->num_jac_equations; jj++) {
		
		//perform the $x$ evaluation
		set_one_d(&linprod_x->coord[jj]); // initialize to 1 for multiplication
		for (ii=0; ii<BED->max_degree; ++ii) {
			dot_product_d(&lin_func_vals->entry[jj][ii],BED->starting_linears[jj][ii],curr_x_vars); // save into a buffer for calculating the derivative later.
			mul_d(&linprod_x->coord[jj], &linprod_x->coord[jj], &lin_func_vals->entry[jj][ii]);// multiply linprod_x times the value we just created
		}
		
		//perform the $v$ evaluation
		dot_product_d(temp, BED->v_linears[jj], curr_v_vars);
		
		//now set the combined $x,v$ value
		mul_d(&start_function_values->coord[jj], &linprod_x->coord[jj], temp); // start_function_values = linprod_x(x)*linear(v)
		mul_d(&linprod_times_gamma_s->coord[jj], gamma_s, &start_function_values->coord[jj]); // sets the value linprod_x*gamma*s
		
		
		neg_d( &Jp->entry[jj + offset][0], &target_function_values->coord[jj]);  // temp = -target
		mul_d( temp, BED->gamma, &start_function_values->coord[jj]);  // temp = gamma*linprod_x(x)*linear(v)
		add_d( &Jp->entry[jj + offset][0], &Jp->entry[jj + offset][0], temp); // Jp = -target + gamma*start
	}
	
//	print_point_to_screen_matlab(target_function_values,"target_f");
//	print_point_to_screen_matlab(target_function_values_times_oneminus_s,"target_f_one_minus_s");
//	print_point_to_screen_matlab(start_function_values,"start_f");
//	print_point_to_screen_matlab(linprod_times_gamma_s,"start_f_gamma_s");
//	print_comp_matlab(gamma_s,"gamma_s");
	
	
	offset = BED->num_randomized_eqns + BED->num_additional_linears; // N-r+k
	for (ii=0; ii<BED->num_jac_equations; ii++) {
		add_d(&funcVals->coord[ii+offset], &target_function_values_times_oneminus_s->coord[ii], &linprod_times_gamma_s->coord[ii]);
	}
	
	
	
	
	
	
	
	
	// DERIVATIVES OF THE HOMOTOPY WRT V
	offset = BED->num_randomized_eqns + BED->num_additional_linears; // N-k+l
	for (ii=0; ii<BED->num_jac_equations; ii++) {
		for (jj=0; jj<BED->num_v_vars; jj++) {
			mul_d(temp, &BED->v_linears[ii]->coord[jj], &linprod_x->coord[ii]);  // temp = M_ij * linprod_x(x)
			mul_d(temp2, temp, gamma_s);                                      // temp2 = gamma*s*M_ij * linprod_x(x)
			
//			std::stringstream converter;
//			converter << "linprod_v_der_" << ii << "_" << jj;
//			print_comp_matlab(temp, converter.str());
//			converter.str("");
			
			mul_d(temp, one_minus_s, &S_times_Jf_pi->entry[ii][jj]);           // temp = (1-s)*(S*[Jf^T pi^T])_ij
			
			add_d(&Jv->entry[ii+offset][jj+BED->num_x_vars], temp, temp2);    // Jv = temp + temp2
		}
	}
	// √ for sphere

//	print_matrix_to_screen_matlab(S_times_Jf_pi,"S_times_Jf_pi");
	
	// now the x derivatives corresponding to the linprod start system
	
	// an implementation of the product rule
	for (mm=0; mm< BED->num_jac_equations; mm++) {
		for (kk=0; kk<BED->num_x_vars; kk++) { // for each variable
			set_zero_d(&linprod_derivative_wrt_x->entry[mm][kk]); // initialize to 0 for the sum
			
			for (ii=0; ii<BED->max_degree; ++ii) { //  for each linear
				
				set_d(running_prod, &BED->starting_linears[mm][ii]->coord[kk]);// initialize the product
				for (jj=0; jj<BED->max_degree; jj++) {
					if (jj!=ii) {
						mul_d(running_prod,running_prod,&lin_func_vals->entry[mm][jj]); // the linear evaluated at curr_var_vals
					}
				}//re: jj
				add_d(&linprod_derivative_wrt_x->entry[mm][kk],&linprod_derivative_wrt_x->entry[mm][kk],running_prod);
				
			}// re:ii
			
			dot_product_d(temp, BED->v_linears[mm], curr_v_vars);  // these two lines multiply by  (v_linear •	v)
			mul_d(&linprod_derivative_wrt_x->entry[mm][kk], &linprod_derivative_wrt_x->entry[mm][kk], temp);
		} // re: kk
	} // re: mm
	
	
	
	
	
	// NUMERICALLY DIFFERENTIATE THE derivative of the target jacobian system wrt $x$.
	
	offset = BED->num_randomized_eqns + BED->num_additional_linears;
	for (ii=0; ii<BED->num_x_vars; ii++) {
		
		//go forward
		vec_cp_d(perturbed_forward_variables, curr_x_vars);
		add_d( &perturbed_forward_variables->coord[ii], &perturbed_forward_variables->coord[ii],BED->perturbation);
		
		
		make_matrix_ID_d(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
		for (kk=0; kk<BED->num_randomized_eqns; kk++)
			for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[kk]-1)); jj++) // -1 because differentiation
				mul_d(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_forward_variables->coord[0]);
		
		for (kk=BED->num_randomized_eqns; kk<BED->num_v_vars; kk++)
			for (jj=0; jj<(BED->max_degree); jj++)
				mul_d(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_forward_variables->coord[0]);
		
		// evaluate perturbed forwards
		evalProg_d(unused_function_values, unused_parVals, unused_parDer,  //  unused output
							 perturbed_Jv,  // <---- the output we need
							 unused_Jp, //unused output
							 perturbed_forward_variables, pathVars, BED->SLP); // input
		
		
		mat_mul_d(perturbed_AtimesJ,BED->randomizer_matrix,perturbed_Jv);
		
		mat_cp_d(tempmat1, BED->jac_with_proj);
		// copy in the transpose of the (randomized) jacobian
		for (mm=0; mm< (BED->num_randomized_eqns); mm++) {
			for (jj=1; jj<BED->num_x_vars; jj++) {
				set_d(&tempmat1->entry[jj - 1][mm],&perturbed_AtimesJ->entry[mm][jj]); 
			}
		}

		mat_mul_d(tempmat2, BED->post_randomizer_matrix, tempmat1);
		mat_mul_d(tempmat3, tempmat2, jac_homogenizing_matrix);
		mul_mat_vec_d(tempvec, tempmat3, curr_v_vars);
		
		
		
		//go backward
		
		vec_cp_d(perturbed_backward_variables, curr_x_vars);
		sub_d(&perturbed_backward_variables->coord[ii], &perturbed_backward_variables->coord[ii],BED->perturbation);
		
		make_matrix_ID_d(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
		for (kk=0; kk<BED->num_randomized_eqns; kk++)
			for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[kk]-1)); jj++)
				mul_d(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_backward_variables->coord[0]);
		for (kk=BED->num_randomized_eqns; kk<BED->num_v_vars; kk++)
			for (jj=0; jj<(BED->max_degree); jj++)
				mul_d(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_backward_variables->coord[0]);
		

		// evaluate perturbed back
		evalProg_d(unused_function_values, unused_parVals, unused_parDer,  //  unused output
							 perturbed_Jv,  // <---- the output we need
							 unused_Jp, //unused output
							 perturbed_backward_variables, pathVars, BED->SLP); // input


		mat_cp_d(tempmat1, BED->jac_with_proj); // probably unnecessary
		
		// copy in the transpose of the (randomized) jacobian
		mat_mul_d(perturbed_AtimesJ,BED->randomizer_matrix,perturbed_Jv);
		for (mm=0; mm< (BED->num_randomized_eqns); mm++) {
			for (jj=1; jj<BED->num_x_vars; jj++) {
				set_d(&tempmat1->entry[jj - 1][mm], &perturbed_AtimesJ->entry[mm][jj]);
			}
		}

		mat_mul_d(tempmat2, BED->post_randomizer_matrix, tempmat1);
		mat_mul_d(tempmat3, tempmat2, jac_homogenizing_matrix);
		mul_mat_vec_d(tempvec2, tempmat3, curr_v_vars);
		
		
		vec_sub_d(tempvec, tempvec, tempvec2); // tempvec = forward - backward

		div_d(temp, BED->half, BED->perturbation);   //this is repetitively wasteful
																								 
		
		
		vec_mulcomp_d(tempvec, tempvec, temp); //tempvec = (forward-backward)/(2h)
		// √ this is verified correct for sphere
		
//		print_point_to_screen_matlab(tempvec,"Jv_target");
		
		vec_mulcomp_d(tempvec, tempvec, one_minus_s);// now tempvec has the numerical derivatives wrt $x$ variable ii. (for all jac eqns)
		
		// now, combine this numerical derivative and the linprod derivative to get the exact values for the $x$ portion of the jacobian matrix to return
		
		for (mm=0; mm<BED->num_jac_equations; mm++) {
			mul_d(temp, gamma_s, &linprod_derivative_wrt_x->entry[mm][ii]);
			add_d(&Jv->entry[mm+offset][ii], &tempvec->coord[mm], temp);
//			std::cout << "setting Jv[" << mm+offset << "][" << ii << "]\n";
		}
		
	}//re: ii for numerical diff

	// END NUMERICAL DIFF wrt x
	
	
	
	//set the X PATCH values
	offset = BED->num_randomized_eqns + BED->num_additional_linears + BED->num_jac_equations;
	
	for (ii = 0; ii<BED->patch.num_patches; ii++)  // for each patch equation
	{ // funcVals = patchValues
		set_d(&funcVals->coord[ii+offset], &patchValues->coord[ii]);
		
		// Jv = Jv_Patch
		for (jj = 0; jj<BED->num_x_vars; jj++) // for each variable
			set_d(&Jv->entry[ii+offset][jj], &Jv_Patch->entry[ii][jj]);
	}
	

	offset = BED->num_randomized_eqns + BED->num_additional_linears + BED->num_jac_equations + BED->patch.num_patches;
	if (offset != BED->num_variables-1) {
		std::cout << "mismatch in number of blabla, line 1006;\n" << offset << " " << BED->num_variables-1 << std::endl;
		print_matrix_to_screen_matlab(Jv,"Jv");
		deliberate_segfault();
	}
	
	// V patch
	set_one_d(temp2);
	dot_product_d(temp, BED->v_patch, curr_v_vars);
	sub_d(&funcVals->coord[BED->num_variables-1], temp, temp2);  // f = patch*v-1

	for (ii=0; ii<BED->num_v_vars; ii++) 
		set_d(&Jv->entry[BED->num_variables-1][BED->num_x_vars+ii], &BED->v_patch->coord[ii]);
	
	
	
	// finally, set parVals & parDer correctly
	
  change_size_point_d(parVals, 1);  change_size_vec_d(parDer, 1);
  parVals->size = parDer->size = 1;
	
  set_d(&parVals->coord[0], pathVars); // s = t
  set_one_d(&parDer->coord[0]);       // ds/dt = 1
	
	
	if (BED->verbose_level>=3) {
	printf("t = %lf+1i*%lf;\n", pathVars->r, pathVars->i);
//	print_matrix_to_screen_matlab(jac_homogenizing_matrix,"jac_hom_1044");
//	print_matrix_to_screen_matlab(BED->post_randomizer_matrix,"S");
//	print_matrix_to_screen_matlab(BED->randomizer_matrix,"R");

	
//	print_matrix_to_screen_matlab( AtimesJ,"jac");
//	print_point_to_screen_matlab(curr_x_vars,"currxvars");
//	print_point_to_screen_matlab(current_variable_values,"curr_vars");
	print_point_to_screen_matlab(funcVals,"F");
	print_matrix_to_screen_matlab(Jv,"Jv");
//	print_matrix_to_screen_matlab(Jp,"Jp");
//	print_matrix_to_screen_matlab(BED->jac_with_proj,"jacwithproj");
	//these values are set in this function:  point_d funcVals, point_d parVals, vec_d parDer, mat_d Jv, mat_d Jp
//	print_matrix_to_screen_matlab(BED->randomizer_matrix,"randomizer_matrix");

//	std::cout << "\n\n**************\n\n";
//	mypause();
	}
	
	
	
	clear_vec_d(curr_x_vars);
	clear_vec_d(curr_v_vars);
	clear_vec_d(patchValues);
	clear_vec_d(temp_function_values);

	
	clear_vec_d(AtimesF);
	clear_vec_d(linprod_x);
	clear_vec_d(linprod_times_gamma_s);
	clear_vec_d(tempvec);
	clear_vec_d(tempvec2);
	

	clear_mat_d(Jv_Patch);
	clear_mat_d(tempmat);
	clear_mat_d(lin_func_vals);
	

	
	clear_mat_d(AtimesJ);
	clear_mat_d(Jv_jac);
	clear_mat_d(temp_jacobian_functions);
	clear_mat_d(temp_jacobian_parameters);
	clear_mat_d(linprod_derivative_wrt_x);
	
	clear_vec_d(start_function_values);
	clear_vec_d(target_function_values);
	clear_vec_d(target_function_values_times_oneminus_s);
	clear_mat_d(S_times_Jf_pi);
	
	
	clear_vec_d(unused_function_values);
	clear_vec_d(unused_parVals);
	clear_vec_d(unused_parDer);

	
	clear_mat_d(unused_Jp);
	clear_mat_d(perturbed_Jv);
	clear_mat_d(perturbed_AtimesJ);
	clear_mat_d(tempmat3);
	clear_mat_d(tempmat1);
	clear_mat_d(tempmat2);

	clear_vec_d(perturbed_forward_variables);
	clear_vec_d(perturbed_backward_variables);

	
	
#ifdef printpathnullspace_left
	BED->num_steps++;
	vec_d dehommed; init_vec_d(dehommed,BED->num_variables-1); dehommed->size = BED->num_variables-1;
	dehomogenize(&dehommed,curr_x_vars);
	fprintf(BED->FOUT,"%.15lf %.15lf ", pathVars->r, pathVars->i);
	for (ii=0; ii<BED->num_variables-1; ++ii) {
		fprintf(BED->FOUT,"%.15lf %.15lf ",dehommed->coord[ii].r,dehommed->coord[ii].i);
	}
	fprintf(BED->FOUT,"\n");
	clear_vec_d(dehommed);
#endif
	
	//	printf("exiting eval\n");
  return 0;
}





void printnullspacejacRelevantData(nullspacejac_eval_data_d *ED_d, nullspacejac_eval_data_mp *ED_mp, int MPType, int eqbyeqMethod, FILE *FP)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: prints the relevant data to FP so that we can recover  *
 * the system if needed for sharpening                           *
 \***************************************************************/
{
  // print the MPType and if an eq-by-eq method (diagona/regen) was used
	//  fprintf(FP, "%d %d\n", MPType, eqbyeqMethod);
	//
	//  // print the patch
	//  printPatchCoeff(FP, MPType, ED_d, ED_mp);
	//
	//  // print the start system
	//  printStartSystem(FP, MPType, ED_d, ED_mp);
	//
	//  // print the square system
	//  printSquareSystem(FP, MPType, ED_d, ED_mp);
	
  return;
}



void nullspacejac_eval_clear_d(nullspacejac_eval_data_d *ED, int clearRegen, int MPType)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: clear ED                                               *
 \***************************************************************/
{
	//clear the patch
  patch_eval_data_clear_d(&ED->patch);
  preproc_data_clear(&ED->preProcData);
	
	
	
  if (MPType == 2)
  { // clear the MP stuff
    nullspacejac_eval_clear_mp(ED->BED_mp, 0, 0);
  }
//TODO: clear properly
	//specifics for the nullspacejac method.
//	clear_vec_d(ED->projection);
	clear_mat_d(ED->randomizer_matrix);
	
#ifdef printpathnullspace_left
	fclose(ED->FOUT);
#endif
	
  return;
}






void setup_nullspacejac_omp_d(int max_threads, endgame_data_t **EG, trackingStats **trackCount_copy, trackingStats *trackCount,
															FILE ***OUT_copy, FILE *OUT, FILE ***RAWOUT_copy, FILE *RAWOUT,
															FILE ***MIDOUT_copy, FILE *MIDOUT, FILE ***FAIL_copy, FILE *FAIL,
															FILE ***NONSOLN_copy, FILE *NONSOLN,
															tracker_config_t **T_copy, tracker_config_t *T,
															nullspacejac_eval_data_d **BED_copy, nullspacejac_eval_data_d *ED_d, nullspacejac_eval_data_mp *ED_mp)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: setup everything needed to do zero dimensional tracking*
 *  using OpenMP                                                 *
 \***************************************************************/
// if max_threads == 1, things are only pointers to the actual values,
// otherwise, they are copies
{
  int ii;
  // error checking
  if (max_threads <= 0)
  {
    printf("\n\nERROR: The number of threads (%d) needs to be positive when setting up for tracking!\n", max_threads);
    bexit(ERROR_CONFIGURATION);
  }
	
	
	
	
  // allocate space for EG
  *EG = (endgame_data_t *)bmalloc(max_threads * sizeof(endgame_data_t));
	
	// initialize
  for (ii = 0; ii < max_threads; ii++)
	{
    if (T->MPType == 2)
    { // initialize for AMP tracking
      init_endgame_data(&(*EG)[ii], 64);
    }
    else
    { // initialize for double precision tracking
      init_endgame_data(&(*EG)[ii], 52);
    }
	}
	
  // allocate space to hold pointers to the files
  *OUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *MIDOUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *RAWOUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *FAIL_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *NONSOLN_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
	
  if (max_threads == 1)
  { // setup the pointers
    *trackCount_copy = trackCount;
    *T_copy = T;
    *BED_copy = ED_d;
    (*BED_copy)->BED_mp = ED_mp; // make sure that this is pointed to inside of ED_d
		
    (*OUT_copy)[0] = OUT;
    (*RAWOUT_copy)[0] = RAWOUT;
    (*MIDOUT_copy)[0] = MIDOUT;
    (*FAIL_copy)[0] = FAIL;
    (*NONSOLN_copy)[0] = NONSOLN;
  }
  else // max_threads > 1
  { // allocate memory
    *trackCount_copy = (trackingStats *)bmalloc(max_threads * sizeof(trackingStats));
    *T_copy = (tracker_config_t *)bmalloc(max_threads * sizeof(tracker_config_t));
    *BED_copy = (nullspacejac_eval_data_d *)bmalloc(max_threads * sizeof(nullspacejac_eval_data_d));
		
    // copy T, ED_d, ED_mp, & trackCount
    for (ii = 0; ii < max_threads; ii++)
    { // copy T
      cp_tracker_config_t(&(*T_copy)[ii], T);
      // copy ED_d & ED_mp
      cp_nullspacejac_eval_data_d(&(*BED_copy)[ii], ED_d, ED_mp, T->MPType);
      // initialize trackCount_copy
      init_trackingStats(&(*trackCount_copy)[ii]);
      (*trackCount_copy)[ii].numPoints = trackCount->numPoints;
    }
		
    // setup the files
    char *str = NULL;
    int size;
    for (ii = 0; ii < max_threads; ii++)
    {
      size = 1 + snprintf(NULL, 0, "output_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "output_%d", ii);
      (*OUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "midout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "midout_%d", ii);
      (*MIDOUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "rawout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "rawout_%d", ii);
      (*RAWOUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "fail_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "fail_%d", ii);
      (*FAIL_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "nonsolutions_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "nonsolutions_%d", ii);
      (*NONSOLN_copy)[ii] = fopen(str, "w+");
    }
    free(str);
  }
	
	
  return;
}

void clear_nullspacejac_omp_d(int max_threads, endgame_data_t **EG, trackingStats **trackCount_copy, trackingStats *trackCount, FILE ***OUT_copy, FILE *OUT, FILE ***RAWOUT_copy, FILE *RAWOUT, FILE ***MIDOUT_copy, FILE *MIDOUT, FILE ***FAIL_copy, FILE *FAIL, FILE ***NONSOLN_copy, FILE *NONSOLN, tracker_config_t **T_copy, nullspacejac_eval_data_d **BED_copy)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: copy the relevant data back to the standard spot and   *
 *  clear the allocated data that was used by OpenMP             *
 \***************************************************************/
// if max_threads == 1, things are only pointers to the actual values,
// otherwise, they are copies
{
  int ii;
	
  // clear EG
  for (ii = max_threads - 1; ii >= 0; ii--)
  {
    clear_endgame_data(&(*EG)[ii]);
  }
  free(*EG);
	
  if (max_threads == 1)
  { // set the pointers to NULL since they just pointed to the actual values
    *trackCount_copy = NULL;
    *T_copy = NULL;
    *BED_copy = NULL;
		
    *OUT_copy[0] = NULL;
    *RAWOUT_copy[0] = NULL;
    *MIDOUT_copy[0] = NULL;
    *FAIL_copy[0] = NULL;
		
    // free the memory of the file pointers
    free(*OUT_copy);
    free(*MIDOUT_copy);
    free(*RAWOUT_copy);
    free(*FAIL_copy);
    free(*NONSOLN_copy);
  }
  else if (max_threads > 1)
  {
    // combine trackCount_copy
    add_trackingStats(trackCount, *trackCount_copy, max_threads);
		
    // clear the copies T, ED_d & ED_mp
    for (ii = max_threads - 1; ii >= 0; ii--)
    { // clear BED_copy - 0 since not using regeneration
      nullspacejac_eval_clear_d(&(*BED_copy)[ii], 0, (*T_copy)[ii].MPType);
			// clear T_copy
      tracker_config_clear(&(*T_copy)[ii]);
    }
		
    // free the memory
    free(*trackCount_copy);
    free(*T_copy);
    free(*BED_copy);
		
    // copy all of the files to the appropriate place
    char ch, *str = NULL;
    int size;
    for (ii = 0; ii < max_threads; ii++)
    {
      // rewind to beginning for reading
      rewind((*OUT_copy)[ii]);
      // copy over to OUT
      ch = fgetc((*OUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(OUT, "%c", ch);
        ch = fgetc((*OUT_copy)[ii]);
      }
      // close file & delete
      fclose((*OUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "output_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "output_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*MIDOUT_copy)[ii]);
      // copy over to MIDOUT
      ch = fgetc((*MIDOUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(MIDOUT, "%c", ch);
        ch = fgetc((*MIDOUT_copy)[ii]);
      }
      // close file & delete
      fclose((*MIDOUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "midout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "midout_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*RAWOUT_copy)[ii]);
      // copy over to RAWOUT
      ch = fgetc((*RAWOUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(RAWOUT, "%c", ch);
        ch = fgetc((*RAWOUT_copy)[ii]);
      }
      // close file & delete
      fclose((*RAWOUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "rawout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "rawout_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*FAIL_copy)[ii]);
      // copy over to FAIL
      ch = fgetc((*FAIL_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(FAIL, "%c", ch);
        ch = fgetc((*FAIL_copy)[ii]);
      }
      // close file & delete
      fclose((*FAIL_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "fail_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "fail_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*NONSOLN_copy)[ii]);
      // copy over to NONSOLN
      ch = fgetc((*NONSOLN_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(NONSOLN, "%c", ch);
        ch = fgetc((*NONSOLN_copy)[ii]);
      }
      // close file & delete
      fclose((*NONSOLN_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "nonsolutions_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "nonsolutions_%d", ii);
      remove(str);
    }
    free(str);
    // free file memory
    free(*OUT_copy);
    free(*MIDOUT_copy);
    free(*RAWOUT_copy);
    free(*FAIL_copy);
    free(*NONSOLN_copy);
  }
	
  return;
}





// derived from zero_dim_basic_setup_d
int nullspacejac_setup_d(FILE **OUT, boost::filesystem::path outName,
												 FILE **midOUT, boost::filesystem::path midName,
												 tracker_config_t *T,
												 nullspacejac_eval_data_d *ED,
												 prog_t *dummyProg,
												 int **startSub, int **endSub, int **startFunc, int **endFunc, int **startJvsub, int **endJvsub, int **startJv, int **endJv, int ***subFuncsBelow,
												 int (**eval_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *),
												 int (**eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *),
												 boost::filesystem::path preprocFile, boost::filesystem::path degreeFile,
												 int findStartPts, boost::filesystem::path pointsIN, boost::filesystem::path pointsOUT,
												 witness_set & W,
												 nullspace_config *ns_config,
												 solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES: number of original variables                   *
 * NOTES: setup for zero dimensional tracking                    *
 \***************************************************************/
{ // need to create the homotopy
  int rank, patchType, ssType, numOrigVars, adjustDegrees, numGps;
	
  *eval_d = &nullspacejac_eval_d;   
  *eval_mp = &nullspacejac_eval_mp;
	
	
  *OUT = safe_fopen_write(outName);  // open the main output files.
  *midOUT = safe_fopen_write(midName);
	
  if (T->MPType == 2) // using AMP - need to allocate space to store BED_mp
    ED->BED_mp = (nullspacejac_eval_data_mp *)bmalloc(1 * sizeof(nullspacejac_eval_data_mp));
  else
    ED->BED_mp = NULL;
	
	
  // setup a straight-line program, using the file(s) created by the parser
  T->numVars = numOrigVars = setupProg_count(dummyProg, T->Precision, T->MPType, startSub, endSub, startFunc, endFunc, startJvsub, endJvsub, startJv, endJv, subFuncsBelow);
	
  // setup preProcData
  setupPreProcData(const_cast<char *>(preprocFile.c_str()), &ED->preProcData);
	
	
	
  numGps = ED->preProcData.num_var_gp + ED->preProcData.num_hom_var_gp;
  // find the rank
  rank = rank_finder_d(&ED->preProcData, dummyProg, T, T->numVars);
	
	patchType = 2; // 1-hom patch
	ssType = 0;    // with 1-hom, we use total degree start system
	
	
	
#ifdef printpathnullspace_left
	int ii;
	ED->FOUT = safe_fopen_write("pathtrack_nullspace_left");
	fprintf(ED->FOUT,"%d ",W.num_variables);
	for (ii=0; ii<W.num_variables; ii++) {
		fprintf(ED->FOUT,"%s ",W.variable_names[ii]);
	}
	fprintf(ED->FOUT,"\n%d ",W.num_pts);
	fprintf(ED->FOUT,"%d %d %d ",T->MPType, T->odePredictor, T->endgameNumber);
	fprintf(ED->FOUT,"\n");
#endif
	
	
	adjustDegrees = 0; // if the system does not need its degrees adjusted, then that is okay
	setupnullspacejacEval_d(T,const_cast<char *>(preprocFile.c_str()),
													const_cast<char *>(degreeFile.c_str()),
													dummyProg,
													rank,
													patchType, ssType, T->MPType,
													&T->numVars, NULL, NULL, NULL,
													ED, adjustDegrees, W,ns_config,solve_options);
	
	
	
	
  return numOrigVars;
}




void setupnullspacejacEval_d(tracker_config_t *T,char preprocFile[], char degreeFile[], prog_t *dummyProg,
														 int squareSize, int patchType, int ssType, int MPType,
														 void const *ptr1, void const *ptr2, void const *ptr3, void const *ptr4,// what are these supposed to point to?
														 nullspacejac_eval_data_d *BED, int adjustDegrees,
														 witness_set & W,
														 nullspace_config *ns_config,
														 solver_configuration *solve_options)
{
  int ii, jj;
	
	BED->verbose_level = solve_options->verbose_level;
	
	BED->num_jac_equations = ns_config->num_jac_equations;
	BED->target_dim = ns_config->target_dim;
	BED->ambient_dim = ns_config->ambient_dim;
	BED->target_crit_codim = ns_config->target_crit_codim;
	
	BED->num_v_vars = ns_config->num_v_vars;
	BED->num_x_vars = ns_config->num_x_vars;
	
	BED->num_randomized_eqns = ns_config->num_randomized_eqns;
	BED->max_degree = ns_config->max_degree;
	BED->randomized_degrees = (int *) br_malloc(ns_config->num_randomized_eqns*sizeof(int));
	for (ii=0; ii<ns_config->randomizer_matrix->rows; ii++)
		BED->randomized_degrees[ii] = ns_config->randomized_degrees[ii]; // store the full degree (not derivative).
	
	BED->num_v_linears = ns_config->num_v_linears;   //

	
	BED->num_variables = ns_config->num_x_vars + ns_config->num_v_vars;
	
  setupPreProcData(preprocFile, &BED->preProcData);

  generic_setup_patch(&BED->patch,W);
	if (T->MPType==2) {
		generic_setup_patch(&BED->BED_mp->patch, W);
	}
	
	
	BED->SLP = dummyProg; // change a pointer
	
	init_mat_d(BED->randomizer_matrix,0,0);
	mat_mp_to_d(BED->randomizer_matrix,
							ns_config->randomizer_matrix);
	
	
	//  THE STUFF PROPRIETARY TO THIS METHOD
	init_mat_d(BED->post_randomizer_matrix,0,0);
	mat_mp_to_d(BED->post_randomizer_matrix,
							ns_config->post_randomizer_matrix);
	
	
	
	// set up the vectors to hold the linears.
	BED->num_projections = ns_config->num_projections;
	BED->target_projection = (vec_d *)br_malloc(ns_config->num_projections * sizeof(vec_d));
	init_mat_d(BED->jac_with_proj, ns_config->num_x_vars-1, ns_config->num_v_vars);
	BED->jac_with_proj->rows = ns_config->num_x_vars-1;
	BED->jac_with_proj->cols = ns_config->num_v_vars;
	
	int offset = ns_config->num_randomized_eqns;
	for (ii=0; ii<ns_config->num_projections; ii++) {
		init_vec_d(BED->target_projection[ii],ns_config->num_x_vars);
		BED->target_projection[ii]->size =  ns_config->num_x_vars;
		vec_mp_to_d(BED->target_projection[ii], ns_config->target_projection[ii]);
		
		for (jj=1; jj<ns_config->num_x_vars; jj++) {
			mp_to_d(&BED->jac_with_proj->entry[jj-1][ii+offset], &ns_config->target_projection[ii]->coord[jj]);
		}
	}
	
	init_vec_d(BED->v_patch,ns_config->num_v_vars);
	BED->v_patch->size = ns_config->num_v_vars;
	vec_mp_to_d(BED->v_patch, ns_config->v_patch);
	
	
	BED->half->r = 0.5;
	BED->half->i = 0.0;
	
	BED->perturbation->r = PERTURBATION_VALUE;
	BED->perturbation->i = PERTURBATION_VALUE; // as defined in the determinant_derivative.h header
	
	
	BED->v_linears = (vec_d *) br_malloc(ns_config->num_v_linears*sizeof(vec_d));
	for (ii=0; ii<ns_config->num_v_linears; ii++) {
		init_vec_d(BED->v_linears[ii],ns_config->num_v_vars);
		BED->v_linears[ii]->size = ns_config->num_v_vars;
		vec_mp_to_d(BED->v_linears[ii], ns_config->v_linears[ii]);
	}

	BED->num_additional_linears = ns_config->num_additional_linears;
	BED->additional_linears_terminal = (vec_d *) br_malloc(ns_config->num_additional_linears*sizeof(vec_d));
	BED->additional_linears_starting = (vec_d *) br_malloc(ns_config->num_additional_linears*sizeof(vec_d));
	
	for (ii=0; ii<ns_config->num_additional_linears; ii++) {
		init_vec_d(BED->additional_linears_terminal[ii], ns_config->num_x_vars);
		BED->additional_linears_terminal[ii]->size = ns_config->num_x_vars;
		vec_mp_to_d(BED->additional_linears_terminal[ii],ns_config->additional_linears_terminal[ii]);
		
		init_vec_d(BED->additional_linears_starting[ii], ns_config->num_x_vars);
		BED->additional_linears_starting[ii]->size = ns_config->num_x_vars;
		vec_mp_to_d(BED->additional_linears_starting[ii],ns_config->additional_linears_starting[ii]);
	}
	
	
	
	BED->starting_linears = (vec_d **)br_malloc(ns_config->num_jac_equations*sizeof(vec_d *));
	
	for (ii=0; ii<ns_config->num_jac_equations; ++ii) {
		BED->starting_linears[ii] = (vec_d *)br_malloc(ns_config->max_degree*sizeof(vec_d));
		for (jj=0; jj<ns_config->max_degree; jj++) {
			init_vec_d(BED->starting_linears[ii][jj],W.num_variables);
			BED->starting_linears[ii][jj]->size = W.num_variables;
			
			vec_mp_to_d(BED->starting_linears[ii][jj], ns_config->starting_linears[ii][jj]);
		}
	}
	
	
	if (solve_options->use_gamma_trick==1)
		get_comp_rand_d(BED->gamma); // set gamma to be random complex value
	else
		set_one_d(BED->gamma);
	
	
	if (MPType == 2)
  { // using AMP - initialize using 16 digits & 64-bit precison
		
#ifdef printpathnullspace_left
		BED->BED_mp->FOUT = BED->FOUT;
#endif
		
		BED->BED_mp->verbose_level = solve_options->verbose_level;
		
		int prec = 64;
		initMP(prec);
		
		BED->BED_mp->curr_prec = prec;
		
		BED->BED_mp->gamma_rat = (mpq_t *)bmalloc(2 * sizeof(mpq_t));
		if (solve_options->use_gamma_trick==1){
			get_comp_rand_rat(BED->gamma, BED->BED_mp->gamma, BED->BED_mp->gamma_rat, prec, T->AMP_max_prec, 1, 1);
		}
		else{
			init_rat(BED->BED_mp->gamma_rat);
			init_mp(BED->BED_mp->gamma);
			set_one_d(BED->gamma);
			set_one_mp(BED->BED_mp->gamma);
			set_one_rat(BED->BED_mp->gamma_rat);
		}
		
		
		BED->BED_mp->SLP = BED->SLP; // assign the SLP pointer
		
		
		
		
		init_mat_mp2(BED->BED_mp->randomizer_matrix,0,0,prec); // initialize the randomizer matrix
		init_mat_mp2(BED->BED_mp->randomizer_matrix_full_prec,0,0,T->AMP_max_prec); // initialize the randomizer matrix
		
		mat_cp_mp(BED->BED_mp->randomizer_matrix_full_prec,ns_config->randomizer_matrix);
		mat_cp_mp(BED->BED_mp->randomizer_matrix,ns_config->randomizer_matrix);
		
		
		BED->BED_mp->num_variables = ns_config->num_x_vars + ns_config->num_v_vars;
		BED->BED_mp->num_jac_equations = ns_config->num_jac_equations;
		BED->BED_mp->target_dim = ns_config->target_dim;
		BED->BED_mp->ambient_dim = ns_config->ambient_dim;
		BED->BED_mp->target_crit_codim = ns_config->target_crit_codim;
		
		BED->BED_mp->num_v_vars = ns_config->num_v_vars;
		BED->BED_mp->num_x_vars = ns_config->num_x_vars;
		
		BED->BED_mp->num_randomized_eqns = ns_config->num_randomized_eqns;
		BED->BED_mp->max_degree = ns_config->max_degree;
		
		
		BED->BED_mp->randomized_degrees.resize(ns_config->num_randomized_eqns);
		for (ii=0; ii<ns_config->randomizer_matrix->rows; ii++)
			BED->BED_mp->randomized_degrees[ii] = ns_config->randomized_degrees[ii]; // store the full degree (not derivative).
		
		BED->BED_mp->num_v_linears = ns_config->num_v_linears;   //
		
		
		
		
		
		
		
		// set the $v$ linears
		
		BED->BED_mp->v_linears = (vec_mp *) br_malloc(ns_config->num_v_linears*sizeof(vec_mp));
		BED->BED_mp->v_linears_full_prec = (vec_mp *) br_malloc(ns_config->num_v_linears*sizeof(vec_mp));
		for (ii=0; ii<ns_config->num_v_linears; ii++) {
			init_vec_mp2(BED->BED_mp->v_linears[ii],ns_config->num_v_vars,prec);
			BED->BED_mp->v_linears[ii]->size = ns_config->num_v_vars;
			
			init_vec_mp2(BED->BED_mp->v_linears_full_prec[ii],ns_config->num_v_vars,T->AMP_max_prec);
			BED->BED_mp->v_linears_full_prec[ii]->size = ns_config->num_v_vars;
			
			vec_cp_mp(BED->BED_mp->v_linears[ii], ns_config->v_linears[ii]);
			vec_cp_mp(BED->BED_mp->v_linears_full_prec[ii], ns_config->v_linears[ii]);
		}
		
		//set the jac_with_proj matrix
		
		init_mat_mp2(BED->BED_mp->jac_with_proj, ns_config->num_x_vars-1, ns_config->num_v_vars, prec);
		BED->BED_mp->jac_with_proj->rows = ns_config->num_x_vars-1;
		BED->BED_mp->jac_with_proj->cols = ns_config->num_v_vars;
		
		init_mat_mp2(BED->BED_mp->jac_with_proj_full_prec, ns_config->num_x_vars-1, ns_config->num_v_vars, T->AMP_max_prec);
		BED->BED_mp->jac_with_proj_full_prec->rows = ns_config->num_x_vars-1;
		BED->BED_mp->jac_with_proj_full_prec->cols = ns_config->num_v_vars;
		
		offset = ns_config->num_randomized_eqns;
		
		// set up the vectors to hold the  linears.
		BED->BED_mp->num_projections = ns_config->num_projections;
		BED->BED_mp->target_projection = (vec_mp *)br_malloc(ns_config->num_projections * sizeof(vec_mp));		
		BED->BED_mp->target_projection_full_prec = (vec_mp *)br_malloc(ns_config->num_projections * sizeof(vec_mp));
		
		for (ii=0; ii<ns_config->num_projections; ii++) {
			init_vec_mp2(BED->BED_mp->target_projection[ii],W.num_variables,prec); BED->BED_mp->target_projection[ii]->size =  W.num_variables;
			vec_cp_mp(BED->BED_mp->target_projection[ii], ns_config->target_projection[ii]);
			
			init_vec_mp2(BED->BED_mp->target_projection_full_prec[ii],W.num_variables,T->AMP_max_prec);
			BED->BED_mp->target_projection_full_prec[ii]->size =  W.num_variables;
			vec_cp_mp(BED->BED_mp->target_projection_full_prec[ii], ns_config->target_projection[ii]);
			
			for (jj=1; jj<ns_config->num_x_vars; jj++) {
				set_mp(&BED->BED_mp->jac_with_proj->entry[jj-1][ii+offset], &ns_config->target_projection[ii]->coord[jj]);
				set_mp(&BED->BED_mp->jac_with_proj_full_prec->entry[jj-1][ii+offset], &ns_config->target_projection[ii]->coord[jj]);
			}
		}
		
		
		

		init_vec_mp2(BED->BED_mp->v_patch_full_prec,ns_config->num_v_vars,T->AMP_max_prec);
		init_vec_mp2(BED->BED_mp->v_patch,ns_config->num_v_vars,prec);
		BED->BED_mp->v_patch->size = BED->BED_mp->v_patch_full_prec->size = ns_config->num_v_vars;
		vec_cp_mp(BED->BED_mp->v_patch_full_prec, ns_config->v_patch);
		vec_cp_mp(BED->BED_mp->v_patch, ns_config->v_patch);
		
		BED->BED_mp->starting_linears = (vec_mp **)br_malloc(ns_config->num_jac_equations*sizeof(vec_mp *));
		BED->BED_mp->starting_linears_full_prec = (vec_mp **)br_malloc(ns_config->num_jac_equations*sizeof(vec_mp *));
		for (ii=0; ii<ns_config->num_jac_equations; ++ii) {
			BED->BED_mp->starting_linears[ii] = (vec_mp *)br_malloc(ns_config->max_degree*sizeof(vec_mp));
			BED->BED_mp->starting_linears_full_prec[ii] = (vec_mp *)br_malloc(ns_config->max_degree*sizeof(vec_mp));
			
			for (jj=0; jj<ns_config->max_degree; jj++) {
				init_vec_mp2(BED->BED_mp->starting_linears[ii][jj],W.num_variables,prec);
				init_vec_mp2(BED->BED_mp->starting_linears_full_prec[ii][jj],W.num_variables,T->AMP_max_prec);
				BED->BED_mp->starting_linears[ii][jj]->size = W.num_variables;
				BED->BED_mp->starting_linears_full_prec[ii][jj]->size = W.num_variables;
				
				vec_cp_mp(BED->BED_mp->starting_linears[ii][jj], ns_config->starting_linears[ii][jj]);
				vec_cp_mp(BED->BED_mp->starting_linears_full_prec[ii][jj], ns_config->starting_linears[ii][jj]);
			}
		}

		BED->BED_mp->num_additional_linears = ns_config->num_additional_linears;
		BED->BED_mp->additional_linears_terminal = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
		BED->BED_mp->additional_linears_terminal_full_prec = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
		
		BED->BED_mp->additional_linears_starting = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
		BED->BED_mp->additional_linears_starting_full_prec = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
		
		for (ii=0; ii<ns_config->num_additional_linears; ii++) {
			init_vec_mp2(BED->BED_mp->additional_linears_terminal[ii], ns_config->num_x_vars,prec);
			init_vec_mp2(BED->BED_mp->additional_linears_terminal_full_prec[ii], ns_config->num_x_vars,T->AMP_max_prec);
			BED->BED_mp->additional_linears_terminal[ii]->size = ns_config->num_x_vars;
			BED->BED_mp->additional_linears_terminal_full_prec[ii]->size = ns_config->num_x_vars;
			vec_cp_mp(BED->BED_mp->additional_linears_terminal[ii],ns_config->additional_linears_terminal[ii]);
			vec_cp_mp(BED->BED_mp->additional_linears_terminal_full_prec[ii],ns_config->additional_linears_terminal[ii]);
			
			
			init_vec_mp2(BED->BED_mp->additional_linears_starting[ii], ns_config->num_x_vars,prec);
			init_vec_mp2(BED->BED_mp->additional_linears_starting_full_prec[ii], ns_config->num_x_vars,T->AMP_max_prec);
			BED->BED_mp->additional_linears_starting[ii]->size = ns_config->num_x_vars;
			BED->BED_mp->additional_linears_starting_full_prec[ii]->size = ns_config->num_x_vars;
			vec_cp_mp(BED->BED_mp->additional_linears_starting[ii],ns_config->additional_linears_starting[ii]);
			vec_cp_mp(BED->BED_mp->additional_linears_starting_full_prec[ii],ns_config->additional_linears_starting[ii]);
		}
		
		init_mat_mp2(BED->BED_mp->post_randomizer_matrix,0,0,prec); // initialize the randomizer matrix
		init_mat_mp2(BED->BED_mp->post_randomizer_matrix_full_prec,0,0,T->AMP_max_prec); // initialize the randomizer matrix
		
		mat_cp_mp(BED->BED_mp->post_randomizer_matrix_full_prec,ns_config->post_randomizer_matrix);
		mat_cp_mp(BED->BED_mp->post_randomizer_matrix,ns_config->post_randomizer_matrix);
		
		init_mp(BED->BED_mp->half);
		init_mp(BED->BED_mp->perturbation);
		d_to_mp(BED->BED_mp->half,BED->half);
		
		BED->half->r = 0.5;
		BED->half->i = 0.0;
		
		comp_d p; p->r = PERTURBATION_VALUE_mp; p->i = PERTURBATION_VALUE_mp;
		d_to_mp(BED->BED_mp->perturbation,p);

		
    // setup preProcData
    setupPreProcData(preprocFile, &BED->BED_mp->preProcData);

  }//re: if mptype==2
	
	
	
	
	
  return;
}



void cp_nullspacejac_eval_data_d(nullspacejac_eval_data_d *BED, nullspacejac_eval_data_d *BED_d_input, nullspacejac_eval_data_mp *BED_mp_input, int MPType)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: stores a copy of BED_(t)_input to BED                  *
 \***************************************************************/
{
	printf("entering cp_nullspacejac_eval_data_d\nthis function needs much attention, as things which should be copied are not!\n");
	exit(-1);
  cp_preproc_data(&BED->preProcData, &BED_d_input->preProcData);
	//  cp_square_system_d(&BED->squareSystem, &BED_d_input->squareSystem);
  cp_patch_d(&BED->patch, &BED_d_input->patch);
	//  cp_start_system_d(&BED->startSystem, &BED_d_input->startSystem);
	BED->SLP = (prog_t *)bmalloc(1 * sizeof(prog_t));
  cp_prog_t(BED->SLP, BED_d_input->SLP);
	
	
	//HERE COPY THE MATRICES  DAB !!!
	
	set_d(BED->gamma, BED_d_input->gamma);
  if (MPType == 2)
  { // need to also setup MP versions since using AMP
    BED->BED_mp = (nullspacejac_eval_data_mp *)bmalloc(1 * sizeof(nullspacejac_eval_data_mp));
		
    cp_preproc_data(&BED->BED_mp->preProcData, &BED_mp_input->preProcData);
    // simply point to the SLP that was setup in BED
		//    cp_square_system_mp(&BED->BED_mp->squareSystem, &BED_mp_input->squareSystem, 0, BED->squareSystem.Prog);
    cp_patch_mp(&BED->BED_mp->patch, &BED_mp_input->patch);
		//    cp_start_system_mp(&BED->BED_mp->startSystem, &BED_mp_input->startSystem);
  }
  else
    BED->BED_mp = NULL;
	
  return;
}




int nullspacejac_dehom(point_d out_d, point_mp out_mp, int *out_prec, point_d in_d, point_mp in_mp, int in_prec, void const *ED_d, void const *ED_mp)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: compute the dehom point                                *
 \***************************************************************/
{
  nullspacejac_eval_data_d *BED_d = NULL;
  nullspacejac_eval_data_mp *BED_mp = NULL;
	
  *out_prec = in_prec;
	
	
	
  if (in_prec < 64)
  { // compute out_d
		nullspacejac_eval_data_d *BED_d = (nullspacejac_eval_data_d *)ED_d;
		
		comp_d denom;
		change_size_vec_d(out_d,in_d->size-1);
		out_d->size = in_d->size-1;
		
		set_d(denom, &in_d->coord[0]);
		
		for (int ii=0; ii<BED_d->num_x_vars-1; ++ii) {
			set_d(&out_d->coord[ii],&in_d->coord[ii+1]);
			div_d(&out_d->coord[ii],&out_d->coord[ii],denom); //  result[ii] = dehom_me[ii+1]/dehom_me[0].
		}
		
		for (int ii=BED_d->num_x_vars-1; ii<in_d->size-1; ++ii) {
			set_d( &out_d->coord[ii],&in_d->coord[ii+1]);
		}
		
		
		
		
		
  }
  else
  { // compute out_mp
		nullspacejac_eval_data_mp *BED_mp = (nullspacejac_eval_data_mp *)ED_mp;
		
		comp_mp denom; init_mp(denom);
		change_size_vec_mp(out_mp,in_mp->size-1);
		out_mp->size = in_mp->size-1;
		
		set_mp(denom, &in_mp->coord[0]);

		for (int ii=0; ii<BED_mp->num_x_vars-1; ++ii) {
			set_mp(&out_mp->coord[ii],&in_mp->coord[ii+1]);
			div_mp(&out_mp->coord[ii],&out_mp->coord[ii],denom); //  result[ii] = dehom_me[ii+1]/dehom_me[0].
		}
		
		for (int ii=BED_mp->num_x_vars-1; ii<in_mp->size-1; ++ii) {
			set_mp( &out_mp->coord[ii],&in_mp->coord[ii+1]);
		}
		
		clear_mp(denom);
		
		
    // set prec on out_mp
    setprec_point_mp(out_mp, *out_prec);
		
		
		
	}
	
	
//	if (in_prec < 64) {
//		print_point_to_screen_matlab(in_d,"in");
//		print_point_to_screen_matlab(out_d,"out");
//	}
//	else{
//		print_point_to_screen_matlab(in_mp,"in");
//		print_point_to_screen_matlab(out_mp,"out");
//	}
	
	
	BED_d = NULL;
  BED_mp = NULL;
	
  return 0;
}









int change_nullspacejac_eval_prec(void const *ED, int new_prec)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: change precision for standard zero dimensional solving *
 \***************************************************************/
{
	nullspacejac_eval_data_mp *BED = (nullspacejac_eval_data_mp *)ED; // to avoid having to cast every time
	
	int ii, jj;
	
	if (new_prec != BED->curr_prec){
		// change the precision for the patch
		changePatchPrec_mp(new_prec, &BED->patch);
		
		if (BED->verbose_level >=4)
			printf("prec  %d\t-->\t%d\n",BED->curr_prec, new_prec);

		BED->SLP->precision = new_prec;
		
		BED->curr_prec = new_prec;
		
		setprec_mp(BED->gamma, new_prec);
		mpf_set_q(BED->gamma->r, BED->gamma_rat[0]);
		mpf_set_q(BED->gamma->i, BED->gamma_rat[1]);
		
		change_prec_mat_mp(BED->randomizer_matrix,new_prec);
		mat_cp_mp(BED->randomizer_matrix,BED->randomizer_matrix_full_prec);
		
		change_prec_mat_mp(BED->post_randomizer_matrix,new_prec);
		mat_cp_mp(BED->post_randomizer_matrix,BED->post_randomizer_matrix_full_prec);
		
		
		for (ii=0; ii<BED->num_additional_linears; ii++) {
			change_prec_point_mp(BED->additional_linears_terminal[ii],new_prec);
			vec_cp_mp(BED->additional_linears_terminal[ii],BED->additional_linears_terminal_full_prec[ii]);
			
			
			change_prec_point_mp(BED->additional_linears_starting[ii],new_prec);
			vec_cp_mp(BED->additional_linears_starting[ii],BED->additional_linears_starting_full_prec[ii]);
		}
		
		
		
		for (ii=0; ii<BED->num_jac_equations; ++ii) {
			for (jj=0; jj<BED->max_degree; jj++) {
				change_prec_point_mp(BED->starting_linears[ii][jj],new_prec);
				vec_cp_mp(BED->starting_linears[ii][jj], BED->starting_linears_full_prec[ii][jj]);
			}
		}
		
		
		
		for (ii=0; ii<BED->num_v_linears; ii++) {
			change_prec_point_mp(BED->v_linears[ii],new_prec);
			vec_cp_mp(BED->v_linears[ii],BED->v_linears_full_prec[ii]);
		}
		
		change_prec_point_mp(BED->v_patch,new_prec);
		vec_cp_mp(BED->v_patch,BED->v_patch_full_prec);
		
		change_prec_mat_mp(BED->jac_with_proj,new_prec);
		mat_cp_mp(BED->jac_with_proj,BED->jac_with_proj_full_prec);
		
		change_prec_mp(BED->perturbation,new_prec);
		change_prec_mp(BED->half,new_prec);
		
		
	}
	
	
  return 0;
}











int nullspacejac_solver_mp(int MPType, //, double parse_time, unsigned int currentSeed
													 witness_set & W,  // includes the initial linear.
													 witness_set *W_new,
													 nullspace_config *ns_config,
													 solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES:                                                        *
 \***************************************************************/
{
	double parse_time = 0;
  FILE *OUT = NULL, *FAIL = fopen("failed_paths", "w"), *midOUT = NULL, *rawOUT = fopen("raw_data", "w");
  tracker_config_t T;
  prog_t dummyProg;
  bclock_t time1, time2;
  int num_variables = 0, num_crossings = 0, num_sols = 0;
	
	
  int *startSub = NULL, *endSub = NULL, *startFunc = NULL, *endFunc = NULL, *startJvsub = NULL, *endJvsub = NULL, *startJv = NULL, *endJv = NULL, **subFuncsBelow = NULL;
  int (*ptr_to_eval_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *) = NULL;
  int (*ptr_to_eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *) = NULL;
	
  nullspacejac_eval_data_mp ED;  // was basic_eval_data_d  DAB
  trackingStats trackCount;
  double track_time;
	
  bclock(&time1); // initialize the clock.
  init_trackingStats(&trackCount); // initialize trackCount to all 0
	
	//necessary for later whatnot
	int userHom = 0, useRegen = 0, pathMod = 0, paramHom = 0;
	
	cp_tracker_config_t(&T, &solve_options->T);
	
	
	// initialize latest_newton_residual_mp
  mpf_init(T.latest_newton_residual_mp);   //<------ THIS LINE IS ABSOLUTELY CRITICAL TO CALL
	
	
	//  // call the setup function
	num_variables = nullspacejac_setup_mp(&OUT, "output",
																				&midOUT, "midpath_data",
																				&T, &ED,
																				&dummyProg,  //arg 7
																				&startSub, &endSub, &startFunc, &endFunc,
																				&startJvsub, &endJvsub, &startJv, &endJv, &subFuncsBelow,
																				&ptr_to_eval_d, &ptr_to_eval_mp,  //args 17,18
																				"preproc_data", "deg.out",
																				!useRegen, "nonhom_start", "start",
																				W,
																				ns_config,
																				solve_options);
  
	int (*change_prec)(void const *, int) = &change_basic_eval_prec;
	
	int (*dehom)(point_d, point_mp, int *, point_d, point_mp, int, void const *, void const *) = &nullspacejac_dehom;
	
	
	
  // error checking
  if (userHom <= 0 && paramHom != 2)
  { // no pathvariables or parameters allowed!
    if (dummyProg.numPathVars > 0)
    { // path variable present
      printf("ERROR: Bertini does not expect path variables when user-defined homotopies are not being used!\n");
      bexit(ERROR_INPUT_SYSTEM);
    }
    if (dummyProg.numPars > 0)
    { // parameter present
      printf("ERROR: Bertini does not expect parameters when user-defined homotopies are not being used!\n");
      bexit(ERROR_INPUT_SYSTEM);
    }
  }
	
	post_process_t *endPoints = (post_process_t *)bmalloc(W.num_pts * sizeof(post_process_t)); //overallocate, expecting full
	
	if (T.endgameNumber == 3)
	{ // use the track-back endgame
		//        zero_dim_trackBack_d(&trackCount, OUT, rawOUT, midOUT, StartPts, FAIL, pathMod, &T, &ED, ED.BED_mp, ptr_to_eval_d, ptr_to_eval_mp, change_basic_eval_prec, zero_dim_dehom);
		printf("bertini_real not equipped to deal with endgameNumber 3\nexiting\n");
		exit(-99);
	}
	else
	{ // use regular endgame
		nullspacejac_track_mp(&trackCount, OUT, rawOUT, midOUT,
													W,  // was the startpts file pointer.
													endPoints,
													FAIL, pathMod,
													&T, &ED,
													ptr_to_eval_mp, //ptr_to_eval_d,
													change_prec, dehom,
													solve_options);
	}
	
	
	
	
	fclose(midOUT);
	
	
	
	
	// finish the output to rawOUT
	fprintf(rawOUT, "%d\n\n", -1);  // bottom of rawOUT
	
	// check for path crossings
	if (solve_options->use_midpoint_checker==1) {
		midpoint_checker(trackCount.numPoints, num_variables,solve_options->midpoint_tol, &num_crossings);
	}
	
	// setup num_sols
	num_sols = trackCount.successes;
	
  // we report how we did with all paths:
  bclock(&time2);
  totalTime(&track_time, time1, time2);
  if (solve_options->verbose_level>=1)
  {
		printf("nullspace_left report:\n");
    printf("Number of failures:  %d\n", trackCount.failures);
    printf("Number of successes:  %d\n", trackCount.successes);
    printf("Number of paths:  %d\n", trackCount.numPoints);
    printf("Parse Time = %fs\n", parse_time);
    printf("Track Time = %fs\n", track_time);
  }
  fprintf(OUT, "Number of failures:  %d\n", trackCount.failures);
  fprintf(OUT, "Number of successes:  %d\n", trackCount.successes);
  fprintf(OUT, "Number of paths:  %d\n", trackCount.numPoints);
  fprintf(OUT, "Parse Time = %fs\n", parse_time);
  fprintf(OUT, "Track Time = %fs\n", track_time);
	

	BRpostProcessing(endPoints, W_new, trackCount.successes, &ED.preProcData, &T, solve_options);
	
	
  // close all of the files
  fclose(OUT);
  fclose(rawOUT);
  fprintf(FAIL, "\n");
  fclose(FAIL);
	

	
	free(startSub);
	free(endSub);
	free(startFunc);
	free(endFunc);
	free(startJvsub);
	free(endJvsub);
	free(startJv);
	free(endJv);
	
	
	
  nullspacejac_eval_clear_mp(&ED, userHom, T.MPType);
  tracker_config_clear(&T);
	
  return 0;
}




void nullspacejac_track_mp(trackingStats *trackCount,
													 FILE *OUT, FILE *RAWOUT, FILE *MIDOUT,
													 witness_set & W,
													 post_process_t *endPoints,
													 FILE *FAIL,
													 int pathMod, tracker_config_t *T,
													 nullspacejac_eval_data_mp *ED,
													 int (*eval_func_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *),
													 int (*change_prec)(void const *, int),
													 int (*find_dehom)(point_d, point_mp, int *, point_d, point_mp, int, void const *, void const *),
													 solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: does standard zero dimensional tracking                *
 *  in either double precision or adaptive precision             *
 \***************************************************************/
{
	
  int ii, oid, max = max_threads();
  tracker_config_t *T_copy = NULL;
  nullspacejac_eval_data_mp *BED_copy = NULL;
  trackingStats *trackCount_copy = NULL;
  FILE **OUT_copy = NULL, **MIDOUT_copy = NULL, **RAWOUT_copy = NULL, **FAIL_copy = NULL, *NONSOLN = NULL, **NONSOLN_copy = NULL;
	
  // top of RAWOUT - number of variables and that we are doing zero dimensional
  fprintf(RAWOUT, "%d\n%d\n", T->numVars, 0);
	

	
  int (*curr_eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *) = &nullspacejac_eval_mp;
	
	
	point_data_mp *startPts = NULL;
	startPts = (point_data_mp *)bmalloc(W.num_pts * sizeof(point_data_mp));
	
	
	
	for (ii = 0; ii < W.num_pts; ii++)
	{ // setup startPts[ii]
		init_point_data_mp2(&startPts[ii], W.num_variables, T->Precision);
		startPts[ii].point->size = W.num_variables;
		
		//NEED TO COPY IN THE WITNESS POINT
		
		//1 set the coordinates
		vec_cp_mp(startPts[ii].point, W.pts_mp[ii] );
		
		//2 set the start time to 1.
		set_one_mp(startPts[ii].time);
	}
	
	
	T->endgameOnly = 0;
	
	
  // setup the rest of the structures
	endgame_data_t *EG = NULL;
  setup_nullspacejac_omp_mp(max,
														&EG, &trackCount_copy, trackCount,
														&OUT_copy, OUT, &RAWOUT_copy, RAWOUT, &MIDOUT_copy, MIDOUT, &FAIL_copy, FAIL, &NONSOLN_copy, NONSOLN,
														&T_copy, T,
														&BED_copy, ED);
	
	
	
	trackCount->numPoints = W.num_pts;
	int solution_counter = 0;
	
	
	
	
	
	
	// track each of the start points
	for (ii = 0; ii < W.num_pts; ii++)
	{ // get current thread number
		oid = thread_num();
		if (solve_options->verbose_level>=1)
			printf("nullspacejac tracking path %d of %d\n",ii,W.num_pts);

		
		
#ifdef printpathnullspace_left
		BED_copy[oid].num_steps = 0;
#endif
		
		generic_track_path_mp(solution_counter, &EG[oid], &startPts[ii], OUT_copy[oid], MIDOUT_copy[oid], &T_copy[oid], &BED_copy[oid], curr_eval_mp, change_prec, find_dehom); //curr_eval_d,
		
#ifdef printpathnullspace_left
		int mm;
		fprintf(BED_copy[oid].FOUT,"-100 %d ",BED_copy[oid].num_steps);
		for (mm=0; mm<BED_copy[oid].num_variables-1; ++mm) {
			fprintf(BED_copy[oid].FOUT,"0 0 ");
		}
		fprintf(BED_copy[oid].FOUT,"\n%d\n\n",EG->retVal);
#endif
		
		
		
		// check to see if it should be sharpened
		if (EG[oid].retVal == 0 && T_copy[oid].sharpenDigits > 0)
		{ // use the sharpener for after an endgame
			sharpen_endpoint_endgame(&EG[oid], &T_copy[oid], OUT_copy[oid], NULL, &BED_copy[oid], NULL, curr_eval_mp, NULL);
			// DAB - replaced curr_eval_d with NULL
		}
		
		
		int issoln = check_issoln_nullspacejac_mp(&EG[oid], &T_copy[oid], &BED_copy[oid]);
		
		
		//get the terminal time in double form
		comp_d time_to_compare;
		mp_to_d(time_to_compare, EG->PD_mp.time);
		
		
		if ((EG->retVal != 0 && time_to_compare->r > T->minTrackT) || !issoln) {  // <-- this is the real indicator of failure...
			trackCount->failures++;
			if (issoln==0) {
				printf("point %d was a non-solution junk point\n",ii);
			}
			else{
				printf("\nretVal = %d; issoln = %d\nthere was a path failure nullspace_left tracking witness point %d\n\n",EG->retVal, issoln,ii);
				print_path_retVal_message(EG->retVal);
			}
		}
		else
		{
			//otherwise converged, but may have still had non-zero retval due to other reasons.
			endgamedata_to_endpoint(&endPoints[solution_counter], EG);
			trackCount->successes++;
			solution_counter++; // probably this could be eliminated
		}
		
		
	}// re: for (ii=0; ii<W.num_pts ;ii++)
	
	
	
	
	//clear the data structures.
	
  for (ii = 0; ii >W.num_pts; ii++)  // clear startPts[ii]
    clear_point_data_mp(&startPts[ii]);
  free(startPts);
	
  // clear the structures
  clear_nullspacejac_omp_mp(max, &EG, &trackCount_copy, trackCount, &OUT_copy, OUT, &RAWOUT_copy, RAWOUT, &MIDOUT_copy, MIDOUT, &FAIL_copy, FAIL, &NONSOLN_copy, NONSOLN, &T_copy, &BED_copy);
	
  return;
}




// derived from zero_dim_basic_setup_d
int nullspacejac_setup_mp(FILE **OUT, boost::filesystem::path outName,
													FILE **midOUT, boost::filesystem::path midName,
													tracker_config_t *T,
													nullspacejac_eval_data_mp *ED,
													prog_t *dummyProg,
													int **startSub, int **endSub, int **startFunc, int **endFunc, int **startJvsub, int **endJvsub, int **startJv, int **endJv, int ***subFuncsBelow,
													int (**eval_d)(point_d, point_d, vec_d, mat_d, mat_d, point_d, comp_d, void const *),
													int (**eval_mp)(point_mp, point_mp, vec_mp, mat_mp, mat_mp, point_mp, comp_mp, void const *),
													boost::filesystem::path preprocFile, boost::filesystem::path degreeFile,
													int findStartPts,
													boost::filesystem::path pointsIN, boost::filesystem::path pointsOUT,
													witness_set & W,
													nullspace_config *ns_config,
													solver_configuration *solve_options)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES: number of original variables                   *
 * NOTES: setup for zero dimensional tracking                    *
 \***************************************************************/
{ // need to create the homotopy
	
  int rank = 0, patchType, ssType, numOrigVars, adjustDegrees, numGps;
	
  *eval_d = &nullspacejac_eval_d;
  *eval_mp = &nullspacejac_eval_mp;
	
  *OUT = safe_fopen_write(outName);  // open the main output files.
  *midOUT = safe_fopen_write(midName);
	
	
	
	
  // setup a straight-line program, using the file(s) created by the parser
  T->numVars = numOrigVars = setupProg_count(dummyProg, T->Precision, T->MPType, startSub, endSub, startFunc, endFunc, startJvsub, endJvsub, startJv, endJv, subFuncsBelow);
	
	
  // setup preProcData
  setupPreProcData(const_cast<char *>(preprocFile.c_str()), &ED->preProcData);
	
	
	
  numGps = ED->preProcData.num_var_gp + ED->preProcData.num_hom_var_gp;
	
	patchType = 2; // 1-hom patch
	ssType = 0;    // with 1-hom, we use total degree start system
	adjustDegrees = 0; // if the system does not need its degrees adjusted, then that is okay
	setupnullspacejacEval_mp(const_cast<char *>(preprocFile.c_str()),
													 const_cast<char *>(degreeFile.c_str()), dummyProg, rank, patchType, ssType, T->Precision, &T->numVars, NULL, NULL, NULL, ED, adjustDegrees, W,ns_config,solve_options);
	
	
	
	
#ifdef printpathnullspace_left
	int ii;
	ED->FOUT = safe_fopen_write("pathtrack_nullspace_left");
	fprintf(ED->FOUT,"%d ",W.num_variables);
	for (ii=0; ii<W.num_variables; ii++) {
		fprintf(ED->FOUT,"%s ",W.variable_names[ii]);
	}
	fprintf(ED->FOUT,"\n%d ",W.num_pts);
	fprintf(ED->FOUT,"%d %d %d ",T->MPType, T->odePredictor, T->endgameNumber);
	fprintf(ED->FOUT,"\n");
#endif
	
  return numOrigVars;
}





//this derived from basic_eval_d
int nullspacejac_eval_mp(point_mp funcVals, point_mp parVals, vec_mp parDer, mat_mp Jv, mat_mp Jp, point_mp current_variable_values, comp_mp pathVars, void const *ED)
{ // evaluates a special homotopy type, built for bertini_real
//	printf("entering eval_mp\n");
  nullspacejac_eval_data_mp *BED = (nullspacejac_eval_data_mp *)ED; // to avoid having to cast every time
	
  int ii, jj, kk, mm;
	int offset;
  comp_mp one_minus_s, gamma_s;  init_mp(one_minus_s); init_mp(gamma_s);
	
	set_one_mp(one_minus_s);
  sub_mp(one_minus_s, one_minus_s, pathVars);  // one_minus_s = (1 - s)
  mul_mp(gamma_s, BED->gamma, pathVars);       // gamma_s = gamma * s
	
  // we assume that the only parameter is s = t and setup parVals & parDer accordingly.
	vec_mp curr_x_vars; init_vec_mp(curr_x_vars, BED->num_x_vars);
	curr_x_vars->size = BED->num_x_vars;
	for (ii=0; ii<BED->num_x_vars; ii++)
		set_mp(&curr_x_vars->coord[ii], &current_variable_values->coord[ii]);
	
	vec_mp curr_v_vars; init_vec_mp(curr_v_vars, BED->num_v_vars);
	curr_v_vars->size = BED->num_v_vars;
	for (ii=0; ii<BED->num_v_vars; ii++)
		set_mp(&curr_v_vars->coord[ii], &current_variable_values->coord[ii+BED->num_x_vars]);
	
	
	
	
	
	
	
	
	vec_mp patchValues; init_vec_mp(patchValues, 0);
	vec_mp temp_function_values; init_vec_mp(temp_function_values,0);
	
	
	vec_mp AtimesF;  init_vec_mp(AtimesF,0);
	vec_mp linprod_x;  init_vec_mp(linprod_x, BED->num_jac_equations);
	linprod_x->size = BED->num_jac_equations;
	vec_mp linprod_times_gamma_s; init_vec_mp(linprod_times_gamma_s,BED->num_jac_equations);
	linprod_times_gamma_s->size = BED->num_jac_equations;
	vec_mp tempvec; init_vec_mp(tempvec,0);
	vec_mp tempvec2; init_vec_mp(tempvec2,0);
	
	
	
	mat_mp Jv_Patch; init_mat_mp(Jv_Patch, 0, 0);
	mat_mp tempmat; init_mat_mp(tempmat,BED->num_variables-1,BED->num_variables-1);
	tempmat->rows = tempmat->cols = BED->num_variables-1; // change the size indicators
	
	mat_mp lin_func_vals; init_mat_mp(lin_func_vals,BED->num_jac_equations, BED->max_degree);
	lin_func_vals->rows = BED->num_jac_equations; lin_func_vals->cols = BED->max_degree;
	
	
	
	
	
	mat_mp AtimesJ; init_mat_mp(AtimesJ,1,1);
	mat_mp Jv_jac; init_mat_mp(Jv_jac,0,0);
	mat_mp temp_jacobian_functions, temp_jacobian_parameters;
	init_mat_mp(temp_jacobian_functions,0,0); init_mat_mp(temp_jacobian_parameters,0,0);
	
	mat_mp linprod_derivative_wrt_x;
	init_mat_mp(linprod_derivative_wrt_x, BED->num_jac_equations, BED->num_x_vars);
	linprod_derivative_wrt_x->rows = BED->num_jac_equations; linprod_derivative_wrt_x->cols = BED->num_x_vars;
	
	
	comp_mp running_prod;  init_mp(running_prod);
	comp_mp temp, temp2, temp3; init_mp(temp); init_mp(temp2); init_mp(temp3);
	
	
	
	mat_mp S_times_Jf_pi;  init_mat_mp(S_times_Jf_pi, BED->post_randomizer_matrix->rows, BED->num_v_vars); // set up temp matrix
	S_times_Jf_pi->rows = BED->post_randomizer_matrix->rows; S_times_Jf_pi->cols = BED->num_v_vars;
	vec_mp target_function_values;  init_vec_mp(target_function_values,0);
	vec_mp target_function_values_times_oneminus_s;  init_vec_mp(target_function_values_times_oneminus_s,0);
	
	vec_mp start_function_values;
	init_vec_mp(start_function_values,BED->num_jac_equations); start_function_values->size = BED->num_jac_equations;
	
	//initialize some containers, for the unused stuff from the called evaluators.
	point_mp unused_function_values, unused_parVals;
	init_vec_mp(unused_function_values,0);init_vec_mp(unused_parVals,0);
	vec_mp unused_parDer; init_vec_mp(unused_parDer,0);
	mat_mp unused_Jp; init_mat_mp(unused_Jp,0,0);
	mat_mp perturbed_Jv; init_mat_mp(perturbed_Jv,0,0);
	
	
	mat_mp perturbed_AtimesJ, tempmat3; // create matrices
	init_mat_mp(perturbed_AtimesJ,0,0); init_mat_mp(tempmat3,0,0);
	
	
	mat_mp tempmat1,tempmat2; // create temp matrices
	init_mat_mp(tempmat1,BED->num_jac_equations,BED->num_v_vars);
	tempmat1->rows = BED->num_jac_equations; tempmat1->cols = BED->num_v_vars;
	
	init_mat_mp(tempmat2,BED->num_jac_equations,BED->num_v_vars);
	tempmat2->rows = BED->num_jac_equations; tempmat2->cols = BED->num_v_vars;
	
	mat_mp jac_homogenizing_matrix; init_mat_mp(jac_homogenizing_matrix,0,0);
	
	//initialize the jacobians we will work with.
	point_mp perturbed_forward_variables, perturbed_backward_variables;
	init_vec_mp(perturbed_forward_variables,0); init_vec_mp(perturbed_backward_variables,0);
	change_size_vec_mp(perturbed_forward_variables,BED->num_x_vars);
	change_size_vec_mp(perturbed_backward_variables,BED->num_x_vars);
	perturbed_backward_variables->size = perturbed_forward_variables->size = BED->num_x_vars;
	
	
	// the main evaluations for $x$
	evalProg_mp(temp_function_values, parVals, parDer, temp_jacobian_functions, temp_jacobian_parameters, curr_x_vars, pathVars, BED->SLP);
	
	
  // evaluate the patch
  patch_eval_mp(    patchValues, parVals, parDer, Jv_Patch, Jp, curr_x_vars, pathVars, &BED->patch);  // Jp is ignored
	
	
	
	
	//resize output variables to correct size
	change_size_vec_mp(funcVals,BED->num_variables);
  change_size_mat_mp(Jv, BED->num_variables, BED->num_variables);
  change_size_mat_mp(Jp, BED->num_variables, 1);
	
	// initialize stuff to all 0's
  funcVals->size = Jv->rows = Jp->rows = BED->num_variables;
  Jv->cols = BED->num_variables;  //  <-- this must be square
  Jp->cols = 1;
	
	for (ii=0; ii<Jv->rows; ii++)
		for (jj=0; jj<Jv->cols; jj++)
			set_zero_mp(&Jv->entry[ii][jj]);
	
	for (ii = 0; ii<BED->num_variables; ii++)
		set_zero_mp(&Jp->entry[ii][0]);  // initialize entire matrix to 0
	
	// orig eqns
	
	
	// randomize
	mul_mat_vec_mp(AtimesF, BED->randomizer_matrix, temp_function_values); // set values of AtimesF (A is randomization matrix)
	
	// set func vals
	for (ii=0; ii<AtimesF->size; ii++)  // for each function, after (real) randomization
		set_mp(&funcVals->coord[ii], &AtimesF->coord[ii]);
	
	
	
	
	// the jacobian equations for orig
	
	//  randomize the original functions and jacobian
	mat_mul_mp(AtimesJ,BED->randomizer_matrix,temp_jacobian_functions);
	
	// copy the jacobian into the return value for the evaluator
	for (ii=0; ii< AtimesJ->rows; ii++)
		for (jj=0; jj< AtimesJ->cols; jj++)
			set_mp(&Jv->entry[ii][jj],&AtimesJ->entry[ii][jj]);
	
	// copy in the transpose of the (randomized) jacobian, omitting the homogenizing variable
	for (ii=0; ii< AtimesJ->rows; ii++)
		for (jj=1; jj<BED->num_x_vars; jj++)
			set_mp(&BED->jac_with_proj->entry[jj-1][ii], &AtimesJ->entry[ii][jj]);
	//√
	
	
	// the additional linears.  there are $r-\ell$ of them.
	offset = BED->num_randomized_eqns;
	for (ii=0; ii< BED->num_additional_linears; ii++) {
		dot_product_mp(temp, BED->additional_linears_terminal[ii], curr_x_vars);
		mul_mp(temp3, temp, one_minus_s);
		neg_mp(&Jp->entry[offset+ii][0], temp);  // Jp = -terminal
		
		dot_product_mp(temp,  BED->additional_linears_starting[ii], curr_x_vars);
		
		mul_mp(temp2, temp, BED->gamma);
		add_mp(&Jp->entry[offset+ii][0], &Jp->entry[offset+ii][0], temp2);   // Jp = -terminal + gamma*start
		
		mul_mp(temp2, temp, gamma_s);
		
		add_mp(&funcVals->coord[offset+ii],temp2, temp3); // (gamma*s)*start(x) + (1-s)*terminal(x)
		
		for (jj=0; jj<BED->num_x_vars; jj++) {
			mul_mp(temp, gamma_s,    &BED->additional_linears_starting[ii]->coord[jj]);
			mul_mp(temp2,one_minus_s,&BED->additional_linears_terminal[ii]->coord[jj]);
			
			add_mp(&Jv->entry[ii+offset][jj], temp, temp2);
		}
	} // √
	
	
	
	// NOW WE WILL WORK ON THE TARGET SYSTEM'S FUNCTION VALUES
	
	
	
	// make the homogenizing matrix for the $x$ variables
	make_matrix_ID_mp(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
	
	for (ii=0; ii<BED->num_randomized_eqns; ii++)
		for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[ii]-1)); jj++)
			mul_mp(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
	
	for (ii=BED->num_randomized_eqns; ii<BED->num_v_vars; ii++)
		for (jj=0; jj<(BED->max_degree); jj++) // these are all degree 1
			mul_mp(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
	
	
	mat_mul_mp(tempmat, BED->post_randomizer_matrix, BED->jac_with_proj); // jac with proj having been set above with unperturbed values
	mat_mul_mp(S_times_Jf_pi, tempmat, jac_homogenizing_matrix); // jac with proj having been set above with unperturbed values
	
	
	mul_mat_vec_mp(target_function_values, S_times_Jf_pi, curr_v_vars);
	vec_mulcomp_mp(target_function_values_times_oneminus_s, target_function_values, one_minus_s);
	
	
	
	//  THE LINPROD START SYSTEM FUNCTION VALUES
	offset = BED->num_randomized_eqns + BED->num_additional_linears;
	// the product of the linears
	for (jj=0; jj<BED->num_jac_equations; jj++) {
		
		//perform the $x$ evaluation
		set_one_mp(&linprod_x->coord[jj]); // initialize to 1 for multiplication
		for (ii=0; ii<BED->max_degree; ++ii) {
			dot_product_mp(&lin_func_vals->entry[jj][ii],BED->starting_linears[jj][ii],curr_x_vars); // save into a buffer for calculating the derivative later.
			mul_mp(&linprod_x->coord[jj], &linprod_x->coord[jj], &lin_func_vals->entry[jj][ii]);// multiply linprod_x times the value we just created
		}
		
		//perform the $v$ evaluation
		dot_product_mp(temp, BED->v_linears[jj], curr_v_vars);
		
		//now set the combined $x,v$ value
		mul_mp(&start_function_values->coord[jj], &linprod_x->coord[jj], temp); // start_function_values = linprod_x(x)*linear(v)
		mul_mp(&linprod_times_gamma_s->coord[jj], gamma_s, &start_function_values->coord[jj]); // sets the value linprod_x*gamma*s
		
		
		neg_mp( &Jp->entry[jj + offset][0], &target_function_values->coord[jj]);  // temp = -target
		mul_mp( temp, BED->gamma, &start_function_values->coord[jj]);  // temp = gamma*linprod_x(x)*linear(v)
		add_mp( &Jp->entry[jj + offset][0], &Jp->entry[jj + offset][0], temp); // Jp = -target + gamma*start
	}
	
	//	print_point_to_screen_matlab(target_function_values,"target_f");
	//	print_point_to_screen_matlab(target_function_values_times_oneminus_s,"target_f_one_minus_s");
	//	print_point_to_screen_matlab(start_function_values,"start_f");
	//	print_point_to_screen_matlab(linprod_times_gamma_s,"start_f_gamma_s");
	//	print_comp_matlab(gamma_s,"gamma_s");
	
	
	offset = BED->num_randomized_eqns + BED->num_additional_linears; // N-r+k
	for (ii=0; ii<BED->num_jac_equations; ii++) {
		add_mp(&funcVals->coord[ii+offset], &target_function_values_times_oneminus_s->coord[ii], &linprod_times_gamma_s->coord[ii]);
	}
	
	
	
	
	
	
	
	
	// DERIVATIVES OF THE HOMOTOPY WRT V
	offset = BED->num_randomized_eqns + BED->num_additional_linears; // N-k+l
	for (ii=0; ii<BED->num_jac_equations; ii++) {
		for (jj=0; jj<BED->num_v_vars; jj++) {
			mul_mp(temp, &BED->v_linears[ii]->coord[jj], &linprod_x->coord[ii]);  // temp = M_ij * linprod_x(x)
			mul_mp(temp2, temp, gamma_s);                                      // temp2 = gamma*s*M_ij * linprod_x(x)
			
			//			std::stringstream converter;
			//			converter << "linprod_v_der_" << ii << "_" << jj;
			//			print_comp_matlab(temp, converter.str());
			//			converter.str("");
			
			mul_mp(temp, one_minus_s, &S_times_Jf_pi->entry[ii][jj]);           // temp = (1-s)*(S*[Jf^T pi^T])_ij
			
			add_mp(&Jv->entry[ii+offset][jj+BED->num_x_vars], temp, temp2);    // Jv = temp + temp2
		}
	}
	// √ for sphere
	
	//	print_matrix_to_screen_matlab(S_times_Jf_pi,"S_times_Jf_pi");
	
	// now the x derivatives corresponding to the linprod start system
	
	// an implementation of the product rule
	for (mm=0; mm< BED->num_jac_equations; mm++) {
		for (kk=0; kk<BED->num_x_vars; kk++) { // for each variable
			set_zero_mp(&linprod_derivative_wrt_x->entry[mm][kk]); // initialize to 0 for the sum
			
			for (ii=0; ii<BED->max_degree; ++ii) { //  for each linear
				
				set_mp(running_prod, &BED->starting_linears[mm][ii]->coord[kk]);// initialize the product
				for (jj=0; jj<BED->max_degree; jj++) {
					if (jj!=ii) {
						mul_mp(running_prod,running_prod,&lin_func_vals->entry[mm][jj]); // the linear evaluated at curr_var_vals
					}
				}//re: jj
				add_mp(&linprod_derivative_wrt_x->entry[mm][kk],&linprod_derivative_wrt_x->entry[mm][kk],running_prod);
				
			}// re:ii
			
			dot_product_mp(temp, BED->v_linears[mm], curr_v_vars);  // these two lines multiply by  (v_linear •	v)
			mul_mp(&linprod_derivative_wrt_x->entry[mm][kk], &linprod_derivative_wrt_x->entry[mm][kk], temp);
		} // re: kk
	} // re: mm
	
	
	
	
	
	// NUMERICALLY DIFFERENTIATE THE derivative of the target jacobian system wrt $x$.
	
	offset = BED->num_randomized_eqns + BED->num_additional_linears;
	for (ii=0; ii<BED->num_x_vars; ii++) {
		
		//go forward
		vec_cp_mp(perturbed_forward_variables, curr_x_vars);
		add_mp( &perturbed_forward_variables->coord[ii], &perturbed_forward_variables->coord[ii],BED->perturbation);
		
		
		make_matrix_ID_mp(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
		for (kk=0; kk<BED->num_randomized_eqns; kk++)
			for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[kk]-1)); jj++) // -1 because differentiation
				mul_mp(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_forward_variables->coord[0]);
		
		for (kk=BED->num_randomized_eqns; kk<BED->num_v_vars; kk++)
			for (jj=0; jj<(BED->max_degree); jj++)
				mul_mp(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_forward_variables->coord[0]);
		
		// evaluate perturbed forwards
		evalProg_mp(unused_function_values, unused_parVals, unused_parDer,  //  unused output
							 perturbed_Jv,  // <---- the output we need
							 unused_Jp, //unused output
							 perturbed_forward_variables, pathVars, BED->SLP); // input
		
		
		mat_mul_mp(perturbed_AtimesJ,BED->randomizer_matrix,perturbed_Jv);
		
		mat_cp_mp(tempmat1, BED->jac_with_proj);
		// copy in the transpose of the (randomized) jacobian
		for (mm=0; mm< (BED->num_randomized_eqns); mm++) {
			for (jj=1; jj<BED->num_x_vars; jj++) {
				set_mp(&tempmat1->entry[jj - 1][mm],&perturbed_AtimesJ->entry[mm][jj]);
			}
		}
		
		mat_mul_mp(tempmat2, BED->post_randomizer_matrix, tempmat1);
		mat_mul_mp(tempmat3, tempmat2, jac_homogenizing_matrix);
		mul_mat_vec_mp(tempvec, tempmat3, curr_v_vars);
		
		
		
		//go backward
		
		vec_cp_mp(perturbed_backward_variables, curr_x_vars);
		sub_mp(&perturbed_backward_variables->coord[ii], &perturbed_backward_variables->coord[ii],BED->perturbation);
		
		make_matrix_ID_mp(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
		for (kk=0; kk<BED->num_randomized_eqns; kk++)
			for (jj=0; jj<(BED->max_degree - (BED->randomized_degrees[kk]-1)); jj++)
				mul_mp(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_backward_variables->coord[0]);
		for (kk=BED->num_randomized_eqns; kk<BED->num_v_vars; kk++)
			for (jj=0; jj<(BED->max_degree); jj++)
				mul_mp(&jac_homogenizing_matrix->entry[kk][kk], &jac_homogenizing_matrix->entry[kk][kk], &perturbed_backward_variables->coord[0]);
		
		
		// evaluate perturbed back
		evalProg_mp(unused_function_values, unused_parVals, unused_parDer,  //  unused output
							 perturbed_Jv,  // <---- the output we need
							 unused_Jp, //unused output
							 perturbed_backward_variables, pathVars, BED->SLP); // input
		
		
		mat_cp_mp(tempmat1, BED->jac_with_proj); // probably unnecessary
		
		// copy in the transpose of the (randomized) jacobian
		mat_mul_mp(perturbed_AtimesJ,BED->randomizer_matrix,perturbed_Jv);
		for (mm=0; mm< (BED->num_randomized_eqns); mm++) {
			for (jj=1; jj<BED->num_x_vars; jj++) {
				set_mp(&tempmat1->entry[jj - 1][mm], &perturbed_AtimesJ->entry[mm][jj]);
			}
		}
		
		mat_mul_mp(tempmat2, BED->post_randomizer_matrix, tempmat1);
		mat_mul_mp(tempmat3, tempmat2, jac_homogenizing_matrix);
		mul_mat_vec_mp(tempvec2, tempmat3, curr_v_vars);
		
		
		vec_sub_mp(tempvec, tempvec, tempvec2); // tempvec = forward - backward
		
		div_mp(temp, BED->half, BED->perturbation);   //this is repetitively wasteful
		
		
		
		vec_mulcomp_mp(tempvec, tempvec, temp); //tempvec = (forward-backward)/(2h)
																					 // √ this is verified correct for sphere
		
		//		print_point_to_screen_matlab(tempvec,"Jv_target");
		
		vec_mulcomp_mp(tempvec, tempvec, one_minus_s);// now tempvec has the numerical derivatives wrt $x$ variable ii. (for all jac eqns)
		
		// now, combine this numerical derivative and the linprod derivative to get the exact values for the $x$ portion of the jacobian matrix to return
		
		for (mm=0; mm<BED->num_jac_equations; mm++) {
			mul_mp(temp, gamma_s, &linprod_derivative_wrt_x->entry[mm][ii]);
			add_mp(&Jv->entry[mm+offset][ii], &tempvec->coord[mm], temp);
			//			std::cout << "setting Jv[" << mm+offset << "][" << ii << "]\n";
		}
		
	}//re: ii for numerical diff
	
	// END NUMERICAL DIFF wrt x
	
	
	
	//set the X PATCH values
	offset = BED->num_randomized_eqns + BED->num_additional_linears + BED->num_jac_equations;
	
	for (ii = 0; ii<BED->patch.num_patches; ii++)  // for each patch equation
	{ // funcVals = patchValues
		set_mp(&funcVals->coord[ii+offset], &patchValues->coord[ii]);
		
		// Jv = Jv_Patch
		for (jj = 0; jj<BED->num_x_vars; jj++) // for each variable
			set_mp(&Jv->entry[ii+offset][jj], &Jv_Patch->entry[ii][jj]);
	}
	
	
	offset = BED->num_randomized_eqns + BED->num_additional_linears + BED->num_jac_equations + BED->patch.num_patches;
	if (offset != BED->num_variables-1) {
		std::cout << "mismatch in number of blabla, line 1006;\n" << offset << " " << BED->num_variables-1 << std::endl;
		print_matrix_to_screen_matlab(Jv,"Jv");
		deliberate_segfault();
	}
	
	// V patch
	set_one_mp(temp2);
	dot_product_mp(temp, BED->v_patch, curr_v_vars);
	sub_mp(&funcVals->coord[BED->num_variables-1], temp, temp2);  // f = patch*v-1
	
	for (ii=0; ii<BED->num_v_vars; ii++)
		set_mp(&Jv->entry[BED->num_variables-1][BED->num_x_vars+ii], &BED->v_patch->coord[ii]);
	
	
	// finally, set parVals & parDer correctly
	
  change_size_point_mp(parVals, 1);  change_size_vec_mp(parDer, 1);
  parVals->size = parDer->size = 1;
	
  set_mp(&parVals->coord[0], pathVars); // s = t
  set_one_mp(&parDer->coord[0]);       // ds/dt = 1

	

	
	if (BED->verbose_level>=3) {
//	print_matrix_to_screen_matlab( AtimesJ,"jac");
//	print_point_to_screen_matlab(curr_x_vars,"currxvars");
		print_point_to_screen_matlab(funcVals,"F_mp");
//	print_point_to_screen_matlab(parVals,"parVals");
//	print_point_to_screen_matlab(parDer,"parDer");
		print_matrix_to_screen_matlab(Jv,"Jv_mp");
//	print_matrix_to_screen_matlab(Jp,"Jp");

//	print_matrix_to_screen_matlab(BED->jac_with_proj,"jacwithproj");
//these values are set in this function:  point_d funcVals, point_d parVals, vec_d parDer, mat_d Jv, mat_d Jp
//	print_matrix_to_screen_matlab(BED->randomizer_matrix,"randomizer_matrix");

//	mypause();
	}
	
	
	
	
	clear_vec_mp(curr_x_vars);
	clear_vec_mp(curr_v_vars);
	clear_vec_mp(patchValues);
	clear_vec_mp(temp_function_values);
	
	
	clear_vec_mp(AtimesF);
	clear_vec_mp(linprod_x);
	clear_vec_mp(linprod_times_gamma_s);
	clear_vec_mp(tempvec);
	clear_vec_mp(tempvec2);
	
	
	clear_mat_mp(Jv_Patch);
	clear_mat_mp(tempmat);
	clear_mat_mp(lin_func_vals);
	
	
	
	clear_mat_mp(AtimesJ);
	clear_mat_mp(Jv_jac);
	clear_mat_mp(temp_jacobian_functions);
	clear_mat_mp(temp_jacobian_parameters);
	clear_mat_mp(linprod_derivative_wrt_x);
	
	clear_mp(running_prod);
	clear_mp(temp);
	clear_mp(temp2);
	clear_mp(temp3);
	
	
	clear_vec_mp(target_function_values);
	clear_vec_mp(target_function_values_times_oneminus_s);
	clear_mat_mp(S_times_Jf_pi);
	
	
	clear_vec_mp(unused_function_values);
	clear_vec_mp(unused_parVals);
	clear_vec_mp(unused_parDer);
	
	
	clear_mat_mp(unused_Jp);
	clear_mat_mp(perturbed_Jv);
	clear_mat_mp(perturbed_AtimesJ);
	clear_mat_mp(tempmat3);
	clear_mat_mp(tempmat1);
	clear_mat_mp(tempmat2);
	
	clear_vec_mp(perturbed_forward_variables);
	clear_vec_mp(perturbed_backward_variables);

	
	
	

	

	
#ifdef printpathnullspace_left
	BED->num_steps++;
	vec_mp dehommed; init_vec_mp(dehommed,BED->num_variables-1); dehommed->size = BED->num_variables-1;
	dehomogenize_mp(&dehommed,curr_x_vars);
	mpf_out_str (BED->FOUT, 10, 15, pathVars->r);
	fprintf(BED->FOUT," ");
	mpf_out_str (BED->FOUT, 10, 15, pathVars->i);
	fprintf(BED->FOUT," ");
	for (ii=0; ii<BED->num_variables-1; ++ii) {
		mpf_out_str (BED->FOUT, 10, 15, dehommed->coord[ii].r);
		fprintf(BED->FOUT," ");
		mpf_out_str (BED->FOUT, 10, 15, dehommed->coord[ii].i);
		fprintf(BED->FOUT," ");
		
	}
	fprintf(BED->FOUT,"\n");
	clear_vec_mp(dehommed);
#endif
	
	
  return 0;
} //re: evaluator mp






void nullspacejac_eval_clear_mp(nullspacejac_eval_data_mp *ED, int clearRegen, int MPType)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: clear ED                                               *
 \***************************************************************/
{
	
  patch_eval_data_clear_mp(&ED->patch);
  preproc_data_clear(&ED->preProcData);
	
	
	//SOME OTHER THINGS OUGHT TO BE CLEARED HERE  DAB
	
	
	//specifics for the nullspacejac method.
	clear_mp(ED->gamma);
	clear_mat_mp(ED->randomizer_matrix);
	
#ifdef printpathnullspace_left
	fclose(ED->FOUT);
#endif
	
  return;
}






void setup_nullspacejac_omp_mp(int max_threads, endgame_data_t **EG, trackingStats **trackCount_copy, trackingStats *trackCount,
															 FILE ***OUT_copy, FILE *OUT, FILE ***RAWOUT_copy, FILE *RAWOUT,
															 FILE ***MIDOUT_copy, FILE *MIDOUT, FILE ***FAIL_copy, FILE *FAIL,
															 FILE ***NONSOLN_copy, FILE *NONSOLN,
															 tracker_config_t **T_copy, tracker_config_t *T,
															 nullspacejac_eval_data_mp **BED_copy, nullspacejac_eval_data_mp *ED_mp)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: setup everything needed to do zero dimensional tracking*
 *  using OpenMP                                                 *
 \***************************************************************/
// if max_threads == 1, things are only pointers to the actual values,
// otherwise, they are copies
{
  int ii;
  // error checking
  if (max_threads <= 0)
  {
    printf("\n\nERROR: The number of threads (%d) needs to be positive when setting up for tracking!\n", max_threads);
    bexit(ERROR_CONFIGURATION);
  }
	
	
	// allocate space for EG
	*EG = (endgame_data_t *)bmalloc(max_threads * sizeof(endgame_data_t));
  for (ii = 0; ii < max_threads; ii++)
    init_endgame_data(&(*EG)[ii], T->Precision);
	
	

	
  // allocate space to hold pointers to the files
  *OUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *MIDOUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *RAWOUT_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *FAIL_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
  *NONSOLN_copy = (FILE **)bmalloc(max_threads * sizeof(FILE *));
	
  if (max_threads == 1)
  { // setup the pointers
    *trackCount_copy = trackCount;
    *T_copy = T;
    *BED_copy = ED_mp;
		//    (*BED_copy)->BED_mp = ED_mp; // make sure that this is pointed to inside of ED_d
		
    (*OUT_copy)[0] = OUT;
    (*RAWOUT_copy)[0] = RAWOUT;
    (*MIDOUT_copy)[0] = MIDOUT;
    (*FAIL_copy)[0] = FAIL;
    (*NONSOLN_copy)[0] = NONSOLN;
  }
  else // max_threads > 1
  { // allocate memory
		printf("more than one thread? prolly gonna crap out here in a sec\n");
		mypause();
		
    *trackCount_copy = (trackingStats *)bmalloc(max_threads * sizeof(trackingStats));
    *T_copy = (tracker_config_t *)bmalloc(max_threads * sizeof(tracker_config_t));
    *BED_copy = (nullspacejac_eval_data_mp *)bmalloc(max_threads * sizeof(nullspacejac_eval_data_mp));
		
    // copy T, ED_d, ED_mp, & trackCount
    for (ii = 0; ii < max_threads; ii++)
    { // copy T
      cp_tracker_config_t(&(*T_copy)[ii], T);
      // copy ED_d & ED_mp
      cp_nullspacejac_eval_data_mp(&(*BED_copy)[ii], ED_mp, T->MPType);
      // initialize trackCount_copy
      init_trackingStats(&(*trackCount_copy)[ii]);
      (*trackCount_copy)[ii].numPoints = trackCount->numPoints;
    }
		
    // setup the files
    char *str = NULL;
    int size;
    for (ii = 0; ii < max_threads; ii++)
    {
      size = 1 + snprintf(NULL, 0, "output_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "output_%d", ii);
      (*OUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "midout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "midout_%d", ii);
      (*MIDOUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "rawout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "rawout_%d", ii);
      (*RAWOUT_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "fail_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "fail_%d", ii);
      (*FAIL_copy)[ii] = fopen(str, "w+");
			
      size = 1 + snprintf(NULL, 0, "nonsolutions_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "nonsolutions_%d", ii);
      (*NONSOLN_copy)[ii] = fopen(str, "w+");
    }
    free(str);
  }
	
	
  return;
}

void clear_nullspacejac_omp_mp(int max_threads, endgame_data_t **EG, trackingStats **trackCount_copy, trackingStats *trackCount, FILE ***OUT_copy, FILE *OUT, FILE ***RAWOUT_copy, FILE *RAWOUT, FILE ***MIDOUT_copy, FILE *MIDOUT, FILE ***FAIL_copy, FILE *FAIL, FILE ***NONSOLN_copy, FILE *NONSOLN, tracker_config_t **T_copy, nullspacejac_eval_data_mp **BED_copy)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: copy the relevant data back to the standard spot and   *
 *  clear the allocated data that was used by OpenMP             *
 \***************************************************************/
// if max_threads == 1, things are only pointers to the actual values,
// otherwise, they are copies
{
  int ii;
	
  // clear EG
  for (ii = max_threads - 1; ii >= 0; ii--)
  {
    clear_endgame_data(&(*EG)[ii]);
  }
  free(*EG);
	
  if (max_threads == 1)
  { // set the pointers to NULL since they just pointed to the actual values
    *trackCount_copy = NULL;
    *T_copy = NULL;
    *BED_copy = NULL;
		
    *OUT_copy[0] = NULL;
    *RAWOUT_copy[0] = NULL;
    *MIDOUT_copy[0] = NULL;
    *FAIL_copy[0] = NULL;
		
    // free the memory of the file pointers
    free(*OUT_copy);
    free(*MIDOUT_copy);
    free(*RAWOUT_copy);
    free(*FAIL_copy);
    free(*NONSOLN_copy);
  }
  else if (max_threads > 1)
  {
    // combine trackCount_copy
    add_trackingStats(trackCount, *trackCount_copy, max_threads);
		
    // clear the copies T, ED_d & ED_mp
    for (ii = max_threads - 1; ii >= 0; ii--)
    { // clear BED_copy - 0 since not using regeneration
      nullspacejac_eval_clear_mp(&(*BED_copy)[ii], 0, (*T_copy)[ii].MPType);
			// clear T_copy
      tracker_config_clear(&(*T_copy)[ii]);
    }
		
    // free the memory
    free(*trackCount_copy);
    free(*T_copy);
    free(*BED_copy);
		
    // copy all of the files to the appropriate place
    char ch, *str = NULL;
    int size;
    for (ii = 0; ii < max_threads; ii++)
    {
      // rewind to beginning for reading
      rewind((*OUT_copy)[ii]);
      // copy over to OUT
      ch = fgetc((*OUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(OUT, "%c", ch);
        ch = fgetc((*OUT_copy)[ii]);
      }
      // close file & delete
      fclose((*OUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "output_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "output_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*MIDOUT_copy)[ii]);
      // copy over to MIDOUT
      ch = fgetc((*MIDOUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(MIDOUT, "%c", ch);
        ch = fgetc((*MIDOUT_copy)[ii]);
      }
      // close file & delete
      fclose((*MIDOUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "midout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "midout_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*RAWOUT_copy)[ii]);
      // copy over to RAWOUT
      ch = fgetc((*RAWOUT_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(RAWOUT, "%c", ch);
        ch = fgetc((*RAWOUT_copy)[ii]);
      }
      // close file & delete
      fclose((*RAWOUT_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "rawout_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "rawout_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*FAIL_copy)[ii]);
      // copy over to FAIL
      ch = fgetc((*FAIL_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(FAIL, "%c", ch);
        ch = fgetc((*FAIL_copy)[ii]);
      }
      // close file & delete
      fclose((*FAIL_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "fail_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "fail_%d", ii);
      remove(str);
			
      // rewind to beginning for reading
      rewind((*NONSOLN_copy)[ii]);
      // copy over to NONSOLN
      ch = fgetc((*NONSOLN_copy)[ii]);
      while (ch != EOF)
      {
        fprintf(NONSOLN, "%c", ch);
        ch = fgetc((*NONSOLN_copy)[ii]);
      }
      // close file & delete
      fclose((*NONSOLN_copy)[ii]);
      size = 1 + snprintf(NULL, 0, "nonsolutions_%d", ii);
      str = (char *)brealloc(str, size * sizeof(char));
      sprintf(str, "nonsolutions_%d", ii);
      remove(str);
    }
    free(str);
    // free file memory
    free(*OUT_copy);
    free(*MIDOUT_copy);
    free(*RAWOUT_copy);
    free(*FAIL_copy);
    free(*NONSOLN_copy);
  }
	
  return;
}




void setupnullspacejacEval_mp(char preprocFile[], char degreeFile[], prog_t *dummyProg,
															int squareSize, int patchType, int ssType, int prec,
															void const *ptr1, void const *ptr2, void const *ptr3, void const *ptr4,
															nullspacejac_eval_data_mp *BED, int adjustDegrees,
															witness_set & W,
															nullspace_config *ns_config,
															solver_configuration *solve_options)
{
  
	int ii, jj;
	
	BED->verbose_level = solve_options->verbose_level;
	
  setupPreProcData(preprocFile, &BED->preProcData);
	
	generic_setup_patch(&BED->patch,W);

	
	BED->SLP = dummyProg;
	
	init_mp2(BED->gamma,prec);
	if (solve_options->use_gamma_trick==1)
		get_comp_rand_mp(BED->gamma); // set gamma to be random complex value
	else
		set_one_mp(BED->gamma);
	

	BED->num_jac_equations = ns_config->num_jac_equations;
	BED->target_dim = ns_config->target_dim;
	BED->ambient_dim = ns_config->ambient_dim;
	BED->target_crit_codim = ns_config->target_crit_codim;
	
	BED->num_v_vars = ns_config->num_v_vars;
	BED->num_x_vars = ns_config->num_x_vars;
	
	BED->num_randomized_eqns = ns_config->num_randomized_eqns;
	BED->max_degree = ns_config->max_degree;
	BED->randomized_degrees.resize(ns_config->num_randomized_eqns);
	for (ii=0; ii<ns_config->randomizer_matrix->rows; ii++)
		BED->randomized_degrees[ii] = ns_config->randomized_degrees[ii]; // store the full degree (not derivative).
	
	
	BED->num_v_linears = ns_config->num_v_linears;   //
	
	
	BED->num_variables = ns_config->num_x_vars + ns_config->num_v_vars;
	
  
	init_mat_mp(BED->randomizer_matrix,0,0);
	mat_cp_mp(BED->randomizer_matrix,
							ns_config->randomizer_matrix);
	
	
	//  THE STUFF PROPRIETARY TO THIS METHOD
	init_mat_mp(BED->post_randomizer_matrix,0,0);
	mat_cp_mp(BED->post_randomizer_matrix,
							ns_config->post_randomizer_matrix);
	
	
	
	// set up the vectors to hold the linears.
	BED->num_projections = ns_config->num_projections;
	BED->target_projection = (vec_mp *)br_malloc(ns_config->num_projections * sizeof(vec_mp));
	init_mat_mp(BED->jac_with_proj, ns_config->num_x_vars-1, ns_config->num_v_vars);
	BED->jac_with_proj->rows = ns_config->num_x_vars-1;
	BED->jac_with_proj->cols = ns_config->num_v_vars;
	
	int offset = ns_config->num_randomized_eqns;
	for (ii=0; ii<ns_config->num_projections; ii++) {
		init_vec_mp(BED->target_projection[ii],ns_config->num_x_vars);
		BED->target_projection[ii]->size =  ns_config->num_x_vars;
		vec_cp_mp(BED->target_projection[ii], ns_config->target_projection[ii]);
		
		for (jj=1; jj<ns_config->num_x_vars; jj++) {
			set_mp(&BED->jac_with_proj->entry[jj-1][ii+offset], &ns_config->target_projection[ii]->coord[jj]);
		}
	}
	
	init_vec_mp(BED->v_patch,ns_config->num_v_vars);
	BED->v_patch->size = ns_config->num_v_vars;
	vec_cp_mp(BED->v_patch, ns_config->v_patch);
	
	comp_d h; h->r = 0.5; h->i = 0;
	init_mp(BED->half);
	d_to_mp(BED->half,h);
	
	init_mp(BED->perturbation);
	comp_d p; p->r = PERTURBATION_VALUE_mp; p->i = PERTURBATION_VALUE_mp;
	d_to_mp(BED->perturbation,p);
	
	
	BED->v_linears = (vec_mp *) br_malloc(ns_config->num_v_linears*sizeof(vec_mp));
	for (ii=0; ii<ns_config->num_v_linears; ii++) {
		init_vec_mp(BED->v_linears[ii],ns_config->num_v_vars);
		BED->v_linears[ii]->size = ns_config->num_v_vars;
		vec_cp_mp(BED->v_linears[ii], ns_config->v_linears[ii]);
	}
	
	BED->num_additional_linears = ns_config->num_additional_linears;
	BED->additional_linears_terminal = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
	BED->additional_linears_starting = (vec_mp *) br_malloc(ns_config->num_additional_linears*sizeof(vec_mp));
	
	for (ii=0; ii<ns_config->num_additional_linears; ii++) {
		init_vec_mp(BED->additional_linears_terminal[ii], ns_config->num_x_vars);
		BED->additional_linears_terminal[ii]->size = ns_config->num_x_vars;
		vec_cp_mp(BED->additional_linears_terminal[ii],ns_config->additional_linears_terminal[ii]);
		
		init_vec_mp(BED->additional_linears_starting[ii], ns_config->num_x_vars);
		BED->additional_linears_starting[ii]->size = ns_config->num_x_vars;
		vec_cp_mp(BED->additional_linears_starting[ii],ns_config->additional_linears_starting[ii]);
	}
	
	
	
	BED->starting_linears = (vec_mp **)br_malloc(ns_config->num_jac_equations*sizeof(vec_mp *));
	
	for (ii=0; ii<ns_config->num_jac_equations; ++ii) {
		BED->starting_linears[ii] = (vec_mp *)br_malloc(ns_config->max_degree*sizeof(vec_mp));
		for (jj=0; jj<ns_config->max_degree; jj++) {
			init_vec_mp(BED->starting_linears[ii][jj],W.num_variables);
			BED->starting_linears[ii][jj]->size = W.num_variables;
			
			vec_cp_mp(BED->starting_linears[ii][jj], ns_config->starting_linears[ii][jj]);
		}
	}
	

	
  return;
}



void cp_nullspacejac_eval_data_mp(nullspacejac_eval_data_mp *BED, nullspacejac_eval_data_mp *BED_mp_input, int MPType)
/***************************************************************\
 * USAGE:                                                        *
 * ARGUMENTS:                                                    *
 * RETURN VALUES:                                                *
 * NOTES: stores a copy of BED_(t)_input to BED                  *
 \***************************************************************/
{
	printf("entering cp_nullspacejac_eval_data_mp\nthis function is likely broken\n");
	exit(-1);
  cp_preproc_data(&BED->preProcData, &BED_mp_input->preProcData);
	//  cp_square_system_d(&BED->squareSystem, &BED_d_input->squareSystem);
  cp_patch_mp(&BED->patch, &BED_mp_input->patch);
	//  cp_start_system_d(&BED->startSystem, &BED_d_input->startSystem);
	BED->SLP = (prog_t *)bmalloc(1 * sizeof(prog_t));
  cp_prog_t(BED->SLP, BED_mp_input->SLP);
	
	
	//HERE COPY THE MATRICES  DAB !!!
	init_mat_mp(BED->randomizer_matrix,0,0);
	mat_cp_mp(BED->randomizer_matrix,BED_mp_input->randomizer_matrix);
	
	set_mp(BED->gamma, BED_mp_input->gamma);
	
	
  return;
}









int check_issoln_nullspacejac_d(endgame_data_t *EG,
																tracker_config_t *T,
																void const *ED)
{
  nullspacejac_eval_data_d *BED = (nullspacejac_eval_data_d *)ED; // to avoid having to cast every time
	
	
	int ii;
	
	vec_d curr_v_vars; init_vec_d(curr_v_vars, BED->num_v_vars);
	curr_v_vars->size = BED->num_v_vars;
	
	vec_d curr_x_vars; init_vec_d(curr_x_vars, BED->num_x_vars);
	curr_x_vars->size = BED->num_x_vars;
	
	double n1, n2, max_rat;
	point_d f;
	eval_struct_d e;
	//
	//	mpf_init(n1); mpf_init(n2); mpf_init(zero_thresh); mpf_init(max_rat);
	init_point_d(f, 1);
	init_eval_struct_d(e,0, 0, 0);
	
	max_rat = T->ratioTol;
	
	// setup threshold based on given threshold and precision
	//	if (num_digits > 300)
	//		num_digits = 300;
	//	num_digits -= 2;
	double tol = MAX(T->funcResTol, 1e-10);
	
	
	if (EG->prec>=64){
		vec_d terminal_pt;  init_vec_d(terminal_pt,1);
		vec_mp_to_d(terminal_pt,EG->PD_mp.point);
		evalProg_d(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, terminal_pt, EG->PD_d.time, BED->SLP);
		//		lin_to_lin_eval_d(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, terminal_pt, EG->PD_d.time, ED);
		

		for (ii=0; ii<BED->num_x_vars; ii++)
			set_d(&curr_x_vars->coord[ii], &terminal_pt->coord[ii]);
		
		for (ii=0; ii<BED->num_v_vars; ii++)
			set_d(&curr_v_vars->coord[ii], &terminal_pt->coord[ii+BED->num_x_vars]);
		
		clear_vec_d(terminal_pt);
	}
	else{
		evalProg_d(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, EG->PD_d.point, EG->PD_d.time, BED->SLP);
		//		lin_to_lin_eval_d(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, EG->PD_d.point, EG->PD_d.time, ED);
		
		for (ii=0; ii<BED->num_x_vars; ii++)
			set_d(&curr_x_vars->coord[ii], &EG->PD_d.point->coord[ii]);
		
		for (ii=0; ii<BED->num_v_vars; ii++)
			set_d(&curr_v_vars->coord[ii], &EG->PD_d.point->coord[ii+BED->num_x_vars]);
		
	}
	
	
	if (EG->last_approx_prec>=64) {
		vec_d prev_pt;  init_vec_d(prev_pt,1);
		vec_mp_to_d(prev_pt,EG->PD_mp.point);
		evalProg_d(f, e.parVals, e.parDer, e.Jv, e.Jp, prev_pt, EG->PD_d.time, BED->SLP);
		clear_vec_d(prev_pt);}
	else{
		evalProg_d(f, e.parVals, e.parDer, e.Jv, e.Jp, EG->last_approx_d, EG->PD_d.time, BED->SLP);
	}
	
	
	
	
	

	
	
//	print_point_to_screen_matlab(EG->PD_d.point,"soln");
//	print_point_to_screen_matlab(e.funcVals,"howfaroff");	// compare the function values
	int isSoln = 1;
	for (ii = 0; (ii < BED->SLP->numFuncs) && isSoln; ii++)
	{
		n1 = d_abs_d( &e.funcVals->coord[ii]); // corresponds to final point
		n2 = d_abs_d( &f->coord[ii]); // corresponds to the previous point
		
		
		if (tol <= n1 && n1 <= n2)
		{ // compare ratio
			if (n1 > max_rat * n2){ // seriously what is the point of this?
				isSoln = 0;
				printf("labeled as non_soln due to max_rat (d) 1 coord %d\n",ii);
			}
		}
		else if (tol <= n2 && n2 <= n1)
		{ // compare ratio
			if (n2 > max_rat * n1){
				isSoln = 0;
				printf("labeled as non_soln due to max_rat (d) 2 coord %d\n",ii);
			}
		}
	}
	

	
	
	if (!isSoln) {
		
		print_point_to_screen_matlab(e.funcVals,"terminal");
		print_point_to_screen_matlab(f,"prev");

		printf("tol was %le\nmax_rat was %le\n",tol,max_rat);
	}
	
	
	
	
//	mat_d AtimesJ; init_mat_d(AtimesJ,0,0);  AtimesJ->rows = AtimesJ->cols = 0;
//	mat_d jac_homogenizing_matrix;  init_mat_d(jac_homogenizing_matrix,0,0);
//	jac_homogenizing_matrix->rows = jac_homogenizing_matrix->cols = 0;
//	mat_d tempmat; init_mat_d(tempmat,0,0); tempmat->rows = tempmat->cols = 0;
//	
//	mat_d Jf_pi; init_mat_d(Jf_pi,0,0); Jf_pi->rows = Jf_pi->cols = 0;
//	vec_d target_function_values; init_vec_d(target_function_values,0); target_function_values->size = 0;
//	
//	mat_mul_d(AtimesJ,BED->randomizer_matrix,e.Jv);
//	for (ii=0; ii< AtimesJ->rows; ii++)
//		for (int jj=1; jj<BED->num_x_vars; jj++)
//			set_d(&BED->jac_with_proj->entry[jj - 1][ii],&AtimesJ->entry[ii][jj]); // copy in the transpose of the (randomized) jacobian, omitting the homogenizing variables
//	
//	make_matrix_ID_d(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
//	
//	for (ii=0; ii<BED->num_randomized_eqns; ii++)
//		for (int jj=0; jj<(BED->max_degree - (BED->randomized_degrees[ii]-1)); jj++)
//			mul_d(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
//	
//	for (ii=BED->num_randomized_eqns; ii<BED->num_v_vars; ii++)
//		for (int jj=0; jj<(BED->max_degree); jj++) // these are all degree 1
//			mul_d(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
//	
//	mat_mul_d(Jf_pi, BED->jac_with_proj, jac_homogenizing_matrix);
//	
//	mul_mat_vec_d(target_function_values, Jf_pi, curr_v_vars);
//	
//	mul_mat_vec_d(target_function_values, BED->post_randomizer_matrix, target_function_values);
//
//	
//	for (ii=0; ii<target_function_values->size; ii++) {
//		if (d_abs_d(&target_function_values->coord[ii]) > tol) {
//			isSoln = 0;
//			print_point_to_screen_matlab(target_function_values,"target_func_vals");
//			break;
//		}
//	}
	
	clear_eval_struct_d(e);
	clear_vec_d(f);
	
//	std::cout << isSoln << std::endl;
	
	
	return isSoln;
	
}


int check_issoln_nullspacejac_mp(endgame_data_t *EG,
																 tracker_config_t *T,
																 void const *ED)
{
  nullspacejac_eval_data_mp *BED = (nullspacejac_eval_data_mp *)ED; // to avoid having to cast every time
	
	int ii;
	
	vec_mp curr_v_vars; init_vec_mp(curr_v_vars, BED->num_v_vars);
	curr_v_vars->size = BED->num_v_vars;
	
	vec_mp curr_x_vars; init_vec_mp(curr_x_vars, BED->num_x_vars);
	curr_x_vars->size = BED->num_x_vars;
	
	
	
	for (ii = 0; ii < T->numVars; ii++)
	{
    if (!(mpfr_number_p(EG->PD_mp.point->coord[ii].r) && mpfr_number_p(EG->PD_mp.point->coord[ii].i)))
		{
			printf("got not a number\n");
			print_point_to_screen_matlab(EG->PD_mp.point,"bad solution");
      return 0;
		}
	}
	
	
	
	mpf_t n1, n2, zero_thresh, max_rat;
	mpf_init(n1); mpf_init(n2); mpf_init(zero_thresh); mpf_init(max_rat);
	
	point_mp f; init_point_mp(f, 1);f->size = 1;
	eval_struct_mp e; init_eval_struct_mp(e, 0, 0, 0);
	
	mpf_set_d(max_rat, T->ratioTol);
	
	
	int num_digits = prec_to_digits((int) mpf_get_default_prec());
	// setup threshold based on given threshold and precision
	if (num_digits > 300)
		num_digits = 300;
	num_digits -= 4;
	double tol = MAX(T->funcResTol, pow(10,-num_digits));
	mpf_set_d(zero_thresh, tol);
	
	
	for (ii=0; ii<BED->num_x_vars; ii++)
		set_mp(&curr_x_vars->coord[ii], &EG->PD_mp.point->coord[ii]);
	
	for (ii=0; ii<BED->num_v_vars; ii++)
		set_mp(&curr_v_vars->coord[ii], &EG->PD_mp.point->coord[ii+BED->num_x_vars]);
	
	
	
	//this one guaranteed by entry condition
	//	lin_to_lin_eval_mp(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, EG->PD_mp.point, EG->PD_mp.time, ED);
	evalProg_mp(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, EG->PD_mp.point, EG->PD_mp.time, BED->SLP);
	
//	print_point_to_screen_matlab(EG->PD_mp.point,"soln");
//	print_point_to_screen_matlab(e.funcVals,"howfaroff");
	
	if (EG->last_approx_prec < 64) { // copy to _mp
		point_d_to_mp(EG->last_approx_mp, EG->last_approx_d);
	}
	
	evalProg_mp(f, e.parVals, e.parDer, e.Jv, e.Jp, EG->last_approx_mp, EG->PD_mp.time, BED->SLP);
	//	lin_to_lin_eval_mp(f,          e.parVals, e.parDer, e.Jv, e.Jp, EG->last_approx_mp, EG->PD_mp.time, ED);
	// compare the function values
	int isSoln = 1;
	for (ii = 0; ii < BED->SLP->numFuncs && isSoln; ii++)
	{
		mpf_abs_mp(n1, &e.funcVals->coord[ii]);
		mpf_abs_mp(n2, &f->coord[ii]);
		
		//		mpf_out_str(NULL,10,9,n1);
		
		if ( (mpf_cmp(zero_thresh, n1) <= 0) &&  (mpf_cmp(n1, n2) <= 0) )
		{ // compare ratio
			mpf_mul(n2, max_rat, n2);
			if (mpf_cmp(n1, n2) > 0){
				isSoln = 0;
				printf("labeled as non_soln due to max_rat (mp) 1\n");
			}
		}
		else if ( (mpf_cmp(zero_thresh, n2) <= 0) &&  (mpf_cmp(n2, n1) <= 0) )
		{ // compare ratio
			mpf_mul(n1, max_rat, n1);
			if (mpf_cmp(n2, n1) > 0){
				isSoln = 0;
				printf("labeled as non_soln due to max_rat (mp) 2\n");
			}
		}
	}
	
	
//	mat_mp AtimesJ; init_mat_mp(AtimesJ,0,0);  AtimesJ->rows = AtimesJ->cols = 0;
//	mat_mp jac_homogenizing_matrix;  init_mat_mp(jac_homogenizing_matrix,0,0);
//	jac_homogenizing_matrix->rows = jac_homogenizing_matrix->cols = 0;
//	mat_mp tempmat; init_mat_mp(tempmat,0,0); tempmat->rows = tempmat->cols = 0;
//	mat_mp S_times_Jf_pi; init_mat_mp(S_times_Jf_pi,0,0); S_times_Jf_pi->rows = S_times_Jf_pi->cols = 0;
//	vec_mp target_function_values; init_vec_mp(target_function_values,0); target_function_values->size = 0;
//	
//	mat_mul_mp(AtimesJ,BED->randomizer_matrix,e.Jv);
//	for (ii=0; ii< AtimesJ->rows; ii++)
//		for (int jj=1; jj<BED->num_x_vars; jj++)
//			set_mp(&BED->jac_with_proj->entry[jj - 1][ii],&AtimesJ->entry[ii][jj]); // copy in the transpose of the (randomized) jacobian, omitting the homogenizing variables
//	make_matrix_ID_mp(jac_homogenizing_matrix,BED->num_v_vars,BED->num_v_vars);
//	
//	for (ii=0; ii<BED->num_randomized_eqns; ii++)
//		for (int jj=0; jj<(BED->max_degree - (BED->randomized_degrees[ii]-1)); jj++)
//			mul_mp(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
//	
//	for (ii=BED->num_randomized_eqns; ii<BED->num_v_vars; ii++)
//		for (int jj=0; jj<(BED->max_degree); jj++) // these are all degree 1
//			mul_mp(&jac_homogenizing_matrix->entry[ii][ii], &jac_homogenizing_matrix->entry[ii][ii], &curr_x_vars->coord[0]);
//	
//	mat_mul_mp(tempmat, BED->post_randomizer_matrix, BED->jac_with_proj); // jac with proj having been set above with unperturbed values
//	mat_mul_mp(S_times_Jf_pi, tempmat, jac_homogenizing_matrix); // jac with proj having been set above with unperturbed values
//	mul_mat_vec_mp(target_function_values, S_times_Jf_pi, curr_v_vars);
//	
//	
//	for (ii=0; ii<target_function_values->size; ii++) {
//		mpf_abs_mp(n1, &target_function_values->coord[ii]);
//		if (mpf_cmp(n1,zero_thresh)>0) {
//			isSoln = 0;
//			std::cout << T->funcResTol << " " <<  pow(10,-num_digits) << std::endl;
//			mpf_out_str (NULL, 10, 6, zero_thresh); // base 10, 6 digits
//			std::cout << std::endl;
//			print_point_to_screen_matlab(target_function_values,"target_func_vals");
//			break;
//		}
//	}
	
	
	
	mpf_clear(n1); mpf_clear(n2); mpf_clear(zero_thresh); mpf_clear(max_rat);
	
	
	clear_eval_struct_mp(e);
	clear_vec_mp(f);
	
//	std::cout << isSoln << std::endl;
	
	return isSoln;
	
}







int check_isstart_nullspacejac_d(point_d testpoint,
																tracker_config_t *T,
																void const *ED)
{

	eval_struct_d e;
	init_eval_struct_d(e,0, 0, 0);
		
	comp_d time;
	set_one_d(time);
	
	
	double tol = (1e-9);
	
	nullspacejac_eval_d(e.funcVals, e.parVals, e.parDer, e.Jv, e.Jp, testpoint, time, ED);
	
	int isSoln = 1;
	
	for (int ii = 0; (ii < e.funcVals->size) && isSoln; ii++) // function by function
	{		
		if (tol <= d_abs_d( &e.funcVals->coord[ii])){ // compare
			isSoln = 0;
			print_point_to_screen_matlab(testpoint,"invalid_startpoint");
			print_point_to_screen_matlab(e.funcVals,"start_residual");
		}
		
	}
	
	
	clear_eval_struct_d(e);
	
	return isSoln;
	
}



void check_nullspace_evaluator(point_mp current_values,
															 void const *ED)
{
	int ii;
	printf("checking homogeneousness of double evaluator\n");
  nullspacejac_eval_data_d *BED = (nullspacejac_eval_data_d *)ED; // to avoid having to cast every time
	//initialize
	eval_struct_d e_d; init_eval_struct_d(e_d, 0, 0, 0);
	eval_struct_d e_d2; init_eval_struct_d(e_d2, 0, 0, 0);
	
	

	
	comp_d zerotime; set_zero_d(zerotime);
	

	
	
	point_d tempvec;  init_point_d(tempvec,0);
	vec_mp_to_d(tempvec, current_values);
	
	nullspacejac_eval_d(e_d.funcVals, e_d.parVals, e_d.parDer, e_d.Jv, e_d.Jp, tempvec, zerotime, ED);

	
	comp_d lambda; get_comp_rand_d(lambda);
	
	for (ii=0; ii<BED->num_x_vars; ii++) {
		mul_d(&tempvec->coord[ii],&tempvec->coord[ii],lambda);
	}
	
	nullspacejac_eval_d(e_d2.funcVals, e_d2.parVals, e_d2.parDer, e_d2.Jv, e_d2.Jp, tempvec, zerotime, ED);
	

	printf("lambda = %lf+1i*%lf\n",lambda->r, lambda->i);
	print_point_to_screen_matlab(e_d.funcVals,"f");
	print_point_to_screen_matlab(e_d2.funcVals,"f2");
	
	
	mypause();

	return;
	
}














//
