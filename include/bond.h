#pragma once
#ifndef BOND_H
#define BOND_H

#include "lpm.h"
#include "unit_cell.h"
#include "particle.h"

template <int nlayer>
class Particle;

template <int nlayer>
class Bond
{
protected:
    static int _ID;

public:
    int id{0};                                                     // identifier of the bond, id starts from 0
    int layer{-1};                                                 // index of current bond layer
    double dis_initial{0}, dis_last{0}, dis{0};                    // initial, last, and current bond length
    double dL{0}, dLe{0}, dLp{0}, dLp_last{0};                     // total, elastic and plastic bond change
    double Kn{0}, Tv{0};                                           // LPM coefficient
    double csx{0}, csy{0}, csz{0};                                 // direction cosine
    double bforce_last{0}, bforce{0}, bdamage{0}, bdamage_last{0}; // bond-wise quantities
    Particle<nlayer> *p1, *p2;                                     // particles are not owned by the bond (only store the location)

    bool updatebGeometry()
    {
        dis = p1->distanceTo(p2);
        dL = dis - dis_initial;
        dLe = dis - dis_initial - dLp;
        csx = (p1->xyz[0] - p2->xyz[0]) / dis;
        csy = (p1->xyz[1] - p2->xyz[1]) / dis;
        csz = (p1->xyz[2] - p2->xyz[2]) / dis;
        if (abs(bdamage - 1.0) < EPS)
        {
            dLe = 0, csx = 0, csy = 0, csz = 0;
            return true; // true means the bond has broken
        }

        return false;
    }

    Bond(Particle<nlayer> *p_p1, Particle<nlayer> *p_p2)
    {
        id = _ID++;
        p1 = p_p1;
        p2 = p_p2;
        dis = p1->distanceTo(p2);
        dis_initial = dis;
        dis_last = dis;
        if ((dis < 1.01 * p1->cell.neighbor_cutoff[0]) && (p1->id != p2->id))
            layer = 0;
        else if ((dis > 1.01 * p1->cell.neighbor_cutoff[0]) && (dis < 1.01 * p1->cell.neighbor_cutoff[1]))
            layer = 1;

        csx = (p1->xyz[0] - p2->xyz[0]) / dis;
        csy = (p1->xyz[1] - p2->xyz[1]) / dis;
        csz = (p1->xyz[2] - p2->xyz[2]) / dis;
    }

    Bond(Particle<nlayer> *p_p1, Particle<nlayer> *p_p2, int p_layer, double p_dis)
    {
        id = _ID++;
        p1 = p_p1;
        p2 = p_p2;
        dis = p_dis;
        dis_initial = p_dis;
        dis_last = dis;
        layer = p_layer;

        csx = (p1->xyz[0] - p2->xyz[0]) / dis;
        csy = (p1->xyz[1] - p2->xyz[1]) / dis;
        csz = (p1->xyz[2] - p2->xyz[2]) / dis;
    }
};

template <int nlayer>
int Bond<nlayer>::_ID = 0;

#endif