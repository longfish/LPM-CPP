#include "lpm.h"
#include "particle.h"
#include "assembly.h"
#include "utilities.h"
#include "stiffness.h"
#include "load_step.h"
#include "solver_static.h"

template <typename T>
void writeMatrix(const char *dataName, T *data, int l)
{
    FILE *fpt;
    fpt = fopen(dataName, "w+");
    for (int i = 0; i < l; i++)
    {
        if constexpr (std::is_integral_v<T>)
        { // constexpr only necessary on first statement
            fprintf(fpt, " %lld\n", data[i]);
        }
        else if (std::is_floating_point_v<T>)
        { // automatically constexpr
            fprintf(fpt, " %.5e\n", data[i]);
        }

        // fprintf(fpt, " %.5e\n", data[i]);
    }

    fclose(fpt);
}

void run()
{
    double start = omp_get_wtime(); // record the CPU time, begin

    const int n_layer = 2; // number of neighbor layers (currently only support 2 layers of neighbors)
    double radius = 0.8;   // particle radius
    UnitCell cell(LatticeType::SimpleCubic3D, radius);

    // Euler angles setting for system rotation
    // flag is 0 ~ 2 for different conventions, (0: direct rotation; 1: Kocks convention; 2: Bunge convention)
    // angle1, angle2 and an angle3 are Euler angles in degree, double
    int eulerflag = 0; // direct rotation
    double angles[] = {PI / 180.0 * 0.0, PI / 180.0 * 0.0, PI / 180.0 * 0.0};
    double *R_matrix = createRMatrix(eulerflag, angles);

    // create a simulation box
    // xmin; xmax; ymin; ymax; zmin; zmax
    std::array<double, 2 * NDIM> box{-0.0, 10.0, -0.4, 10.0, -0.4, 30.0};

    std::vector<std::array<double, NDIM>> sc_xyz = createCuboidSC3D(box, cell, R_matrix);
    Assembly<n_layer> pt_ass{sc_xyz, box, cell, ParticleType::Elastic}; // elastic bond
    printf("\nParticle number is %d\n", pt_ass.nparticle);

    // material elastic parameters setting, MPa
    double E0 = 69e3, mu0 = 0.3; // Young's modulus and Poisson's ratio
    std::vector<Particle<n_layer> *> top_group, bottom_group, internal_group;
    for (Particle<n_layer> *p1 : pt_ass.pt_sys)
    {
        // assign boundary and internal particles
        if (p1->xyz[2] > box[5] - 2 * radius)
        {
            top_group.push_back(p1); // top
            p1->type = 1;
        }
        if (p1->xyz[2] < box[4] + 2 * radius)
        {
            bottom_group.push_back(p1); // bottom
            p1->type = 2;
        }
        if (p1->nb == cell.nneighbors)
            internal_group.push_back(p1); // particles with full neighbor list

        // assign material properties - need to cast to elastic particle
        ParticleElastic<n_layer> *elpt = dynamic_cast<ParticleElastic<n_layer> *>(p1);
        elpt->setParticleProperty(E0, mu0);
    }

    pt_ass.updateGeometry();
    pt_ass.updateForceState();

    // simulation settings
    int n_steps = 1; // number of loading steps
    // double step_size = -1e-3; // step size for displacement loading
    double step_size = -2000; // step size for force loading

    std::vector<LoadStep<n_layer>> load; // load settings for multiple steps
    for (int i = 0; i < n_steps; i++)
    {
        LoadStep<n_layer> step;

        // boundary conditions
        step.dispBCs.push_back(DispBC<n_layer>(top_group, LoadMode::Relative, 'x', 0.0));
        step.dispBCs.push_back(DispBC<n_layer>(top_group, LoadMode::Relative, 'y', 0.0));
        step.dispBCs.push_back(DispBC<n_layer>(top_group, LoadMode::Relative, 'z', 0.0));
        step.forceBCs.push_back(ForceBC<n_layer>(bottom_group, LoadMode::Relative, 0.0, 0.0, -step_size));
        // step.dispBCs.push_back(DispBC<n_layer>(bottom_group, 'z', step_size));
        load.push_back(step);
    }

    double initrun = omp_get_wtime();
    printf("Initialization finished in %f seconds\n\n", initrun - start);

    int max_iter = 30;                                                                                                         /* maximum Newton iteration number */
    double tol_iter = 1e-5;                                                                                                    /* newton iteration tolerance */
    SolverStatic<n_layer> solv{pt_ass, StiffnessMode::Analytical, SolverMode::CG, "result_position.dump", max_iter, tol_iter}; // stiffness mode and solution mode

    // write down global matrices
    int start_index{0};
    solv.solveProblem(load, start_index);
    writeMatrix("matrix_K_global.txt", solv.stiffness.K_global, solv.stiffness.K_pointer[pt_ass.pt_sys.size()]);
    writeMatrix("matrix_K_pointer.txt", solv.stiffness.K_pointer, pt_ass.pt_sys.size() + 1);
    writeMatrix("matrix_IK.txt", solv.stiffness.IK, pt_ass.pt_sys[0]->cell.dim * pt_ass.pt_sys.size() + 1);
    writeMatrix("matrix_JK.txt", solv.stiffness.JK, solv.stiffness.K_pointer[pt_ass.pt_sys.size()]);

    double finish = omp_get_wtime();
    printf("Computation time for total steps: %f seconds\n\n", finish - start);
}