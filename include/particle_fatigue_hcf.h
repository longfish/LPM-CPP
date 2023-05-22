#pragma once
#ifndef PARTICLE_FATIGUE_HCF_H
#define PARTICLE_FATIGUE_HCF_H

#include <vector>
#include <array>

#include "lpm.h"
#include "unit_cell.h"
#include "particle.h"
#include "bond.h"

// Elastic plane strain or 3D material
// Update bdamage and bforce after geometry calculation
// Four particle-wise state variables: [0]damage, [1]counter, [2]eq_stress1, [3]eq_stress2

template <int nlayer>
class ParticleFatigueHCF : public Particle<nlayer>
{
public:
    double A{0}, b{0}, c{0}; // damage parameters
    double sigma_TS{0};      // tensile strength
    double eta{0.2};         // fatigue cycle jumping parameter
    double eq_stress_a{0};   // equivalent stress amplitude
    double Ddot{0};          // damage rate
    double damage_threshold{0.35}, critical_bstrain{1.2e-3};

    ParticleFatigueHCF(const double &p_x, const double &p_y, const double &p_z, const UnitCell &p_cell) : Particle<nlayer>{p_x, p_y, p_z, p_cell}
    {
        this->state_var = std::vector<double>(4, 0.);
        this->state_var_last = std::vector<double>(4, 0.);
    }

    ParticleFatigueHCF(const double &p_x, const double &p_y, const double &p_z, const UnitCell &p_cell, const int &p_type) : Particle<nlayer>{p_x, p_y, p_z, p_cell, p_type}
    {
        this->state_var = std::vector<double>(4, 0.);
        this->state_var_last = std::vector<double>(4, 0.);
    }

    double calcEqStress();
    int calcNCycleJump();

    bool updateParticleStateVariables();
    bool updateParticleBrokenBonds();

    void updateBondsForce();
    void setParticleProperty(double p_E, double p_mu, double p_sigma_TS, double p_A, double p_b, double p_c, double p_eta);
    void setParticleProperty(double p_C11, double p_C12, double p_C44, double p_sigma_TS, double p_A, double p_b, double p_c, double p_eta);
};

template <int nlayer>
bool ParticleFatigueHCF<nlayer>::updateParticleBrokenBonds()
{
    bool any_broken{false};
    for (int i = 0; i < nlayer; ++i)
    {
        for (Bond<nlayer> *bd : this->bond_layers[i])
        {
            if (bd->bstrain >= critical_bstrain && abs(bd->bdamage - 1.0) > EPS)
            {
                any_broken = any_broken || true;
                bd->bdamage = 1;
                //--(this->nb);
            }
        }
    }
    return any_broken;
}

template <int nlayer>
void ParticleFatigueHCF<nlayer>::updateBondsForce()
{
    // calculate the current bond force using current damage value
    for (int i = 0; i < nlayer; ++i)
    {
        for (Bond<nlayer> *bd : this->bond_layers[i])
        {
            bd->bforce = 2. * bd->Kn * bd->dLe + 2. * bd->Tv * this->dLe_total[bd->layer];           // trial elastic bforce
            bd->bdamage = std::max(bd->bdamage, std::max(this->state_var[0], bd->p2->state_var[0])); // update the bond-wise damage
            bd->bforce *= (1.0 - bd->bdamage);
        }
    }
}

template <int nlayer>
int ParticleFatigueHCF<nlayer>::calcNCycleJump()
{
    double coef = eta * pow((1 - this->state_var[0]), b + 1) / A / b;
    double para = pow(sigma_TS / eq_stress_a, c);
    // std::cout << eq_stress_a << std::endl;
    if (coef * para > 2e5)
        return 200000;
    return (int)(coef * para);
}

template <int nlayer>
double ParticleFatigueHCF<nlayer>::calcEqStress()
{
    // compute local stress tensor
    std::vector<double> stress_local(6, 0);
    for (int i = 0; i < nlayer; ++i)
    {
        for (Bond<nlayer> *bd : this->bond_layers[i])
        {
            stress_local[0] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csx * bd->csx * this->cell.nneighbors / this->nb;
            stress_local[1] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csy * bd->csy * this->cell.nneighbors / this->nb;
            stress_local[2] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csz * bd->csz * this->cell.nneighbors / this->nb;
            stress_local[3] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csy * bd->csz * this->cell.nneighbors / this->nb;
            stress_local[4] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csx * bd->csz * this->cell.nneighbors / this->nb;
            stress_local[5] += 0.5 / this->cell.particle_volume * bd->dis * bd->bforce * bd->csx * bd->csy * this->cell.nneighbors / this->nb;
        }
    }

    /* update stress tensor to be trial devitoric stress tensor */
    double temp = 1.0 / 3.0 * (stress_local[0] + stress_local[1] + stress_local[2]);
    for (int j = 0; j < NDIM; j++)
        stress_local[j] -= temp;

    /* von Mises equivalent stress */
    double sigma_eq = 0.0;
    for (int j = 0; j < 2 * NDIM; j++)
    {
        if (j < NDIM)
            sigma_eq += stress_local[j] * stress_local[j]; // s11, s22, s33
        else
            sigma_eq += 2.0 * stress_local[j] * stress_local[j]; // s23, s13, s12
    }
    sigma_eq = sqrt(3.0 / 2.0 * sigma_eq);

    return sigma_eq;
}

template <int nlayer>
bool ParticleFatigueHCF<nlayer>::updateParticleStateVariables()
{
    // if counter == 0 or 1
    // compute equivalent stress and store it
    int counter = this->state_var[1];
    if (counter == 0 || counter == 1)
        this->state_var[counter + 2] = calcEqStress();

    // if counter == 2
    // compute the eq stress amplitude
    // compute Ddot
    // update D
    if (counter == 2)
    {
        eq_stress_a = abs(0.5 * (this->state_var[2] + this->state_var[3]));
        Ddot = A * pow((eq_stress_a / sigma_TS), c) / pow(1 - this->state_var[0], b);
    }

    // if counter == 3
    // update D
    if (counter == 3)
        this->state_var[0] += Ddot * this->ncycle_jump;

    this->state_var[1] += 1; // counter++;

    return false;
}

template <int nlayer>
void ParticleFatigueHCF<nlayer>::setParticleProperty(double p_E, double p_mu, double p_sigma_TS, double p_A, double p_b, double p_c, double p_eta)
{
    sigma_TS = p_sigma_TS;
    A = p_A;
    b = p_b;
    c = p_c;
    eta = p_eta;

    double Ce[NDIM]{p_E * (1.0 - p_mu) / (1.0 + p_mu) / (1.0 - 2.0 * p_mu),
                    p_E * p_mu / (1.0 + p_mu) / (1.0 - 2.0 * p_mu),
                    p_E / 2.0 / (1.0 + p_mu)}; // C11, C12, C44
    double KnTv[NDIM]{0};

    if (this->cell.dim == 2)
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, NDIM, 1, NDIM, 1.0, this->cell.el_mapping.data(), 3, Ce, 1, 0.0, KnTv, 1);
    else
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, NDIM, 1, NDIM, this->cell.radius, this->cell.el_mapping.data(), 3, Ce, 1, 0.0, KnTv, 1);

    for (int i = 0; i < nlayer; ++i)
    {
        for (Bond<nlayer> *bd : this->bond_layers[i])
        {
            if (this->cell.lattice == LatticeType::Hexagon2D)
            {
                bd->Kn = KnTv[0];
                bd->Tv = KnTv[1];
            }
            else
            {
                bd->Kn = KnTv[bd->layer]; // layer is 0 or 1
                bd->Tv = KnTv[2];
            }
        }
    }
}

template <int nlayer>
void ParticleFatigueHCF<nlayer>::setParticleProperty(double p_C11, double p_C12, double p_C44, double p_sigma_TS, double p_A, double p_b, double p_c, double p_eta)
{
    sigma_TS = p_sigma_TS;
    A = p_A;
    b = p_b;
    c = p_c;
    eta = p_eta;

    double Ce[NDIM]{p_C11, p_C12, p_C44}; // C11, C12, C44
    double KnTv[NDIM]{0};
    if (this->cell.dim == 2)
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, NDIM, 1, NDIM, 1.0, this->cell.el_mapping.data(), 3, Ce, 1, 0.0, KnTv, 1);
    else
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, NDIM, 1, NDIM, this->cell.radius, this->cell.el_mapping.data(), 3, Ce, 1, 0.0, KnTv, 1);

    for (int i = 0; i < nlayer; ++i)
    {
        for (Bond<nlayer> *bd : this->bond_layers[i])
        {
            if (this->cell.lattice == LatticeType::Hexagon2D)
            {
                bd->Kn = KnTv[0];
                bd->Tv = KnTv[1];
            }
            else
            {
                bd->Kn = KnTv[bd->layer]; // layer is 0 or 1
                bd->Tv = KnTv[2];
            }
        }
    }
}

#endif