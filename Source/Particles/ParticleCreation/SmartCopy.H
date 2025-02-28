/* Copyright 2019-2020 Andrew Myers, Axel Huebl,
 *                     Maxence Thevenet
 *
 * This file is part of WarpX.
 *
 * License: BSD-3-Clause-LBNL
 */
#ifndef WARPX_SMART_COPY_H_
#define WARPX_SMART_COPY_H_

#include "DefaultInitialization.H"
#include "SmartUtils.H"

#include <AMReX_GpuContainers.H>
#include <AMReX_ParallelDescriptor.H>

#include <map>
#include <string>

/**
 * \brief This is a functor for performing a "smart copy" that works
 * in both host and device code.
 *
 * A "smart" copy does the following. First, the destination particle
 * components are initialized to the default values for that component
 * type. Second, if a given component name is found in both the src
 * and the dst, then the src value is copied.
 *
 * Particle positions and id numbers are always copied.
 *
 * You don't create this directly - use the SmartCopyFactory object below.
 */
struct SmartCopy
{
    int m_num_copy_real;
    const int* m_src_comps_r;
    const int* m_dst_comps_r;

    int m_num_copy_int;
    const int* m_src_comps_i;
    const int* m_dst_comps_i;

    const InitializationPolicy* m_policy_real;
    const InitializationPolicy* m_policy_int;

    template <typename DstData, typename SrcData>
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    void operator() (DstData& dst, const SrcData& src, int i_src, int i_dst,
                     amrex::RandomEngine const& engine) const noexcept
    {
        // initialize the real components
        for (int j = 0; j < DstData::NAR; ++j) {
            dst.m_rdata[j][i_dst] = initializeRealValue(m_policy_real[j], engine);
        }
        for (int j = 0; j < dst.m_num_runtime_real; ++j) {
            dst.m_runtime_rdata[j][i_dst] = initializeRealValue(m_policy_real[j+DstData::NAR], engine);
        }

        // initialize the int components
        for (int j = 0; j < DstData::NAI; ++j) {
            dst.m_idata[j][i_dst] = initializeIntValue(m_policy_int[j]);
        }
        for (int j = 0; j < dst.m_num_runtime_int; ++j) {
            dst.m_runtime_idata[j][i_dst] = initializeIntValue(m_policy_int[j+DstData::NAI]);
        }

        // copy the shared real components
        for (int j = 0; j < m_num_copy_real; ++j)
        {
            int src_comp, dst_comp;
            amrex::ParticleReal* AMREX_RESTRICT dst_data;
            const amrex::ParticleReal* AMREX_RESTRICT src_data;

            if (m_src_comps_r[j] < SrcData::NAR)
            {
                // This is a compile-time attribute of the src
                src_comp = m_src_comps_r[j];
                src_data = src.m_rdata[src_comp];
            }
            else
            {
                // This is a runtime attribute of the src
                src_comp = m_src_comps_r[j] - SrcData::NAR;
                src_data = src.m_runtime_rdata[src_comp];
            }

            if (m_dst_comps_r[j] < DstData::NAR)
            {
                // This is a compile-time attribute of the dst
                dst_comp = m_dst_comps_r[j];
                dst_data = dst.m_rdata[dst_comp];
            }
            else
            {
                // This is a runtime attribute of the dst
                dst_comp = m_dst_comps_r[j] - DstData::NAR;
                dst_data = dst.m_runtime_rdata[dst_comp];
            }

            dst_data[i_dst] = src_data[i_src];
        }

        // copy the shared int components
        for (int j = 0; j < m_num_copy_int; ++j)
        {
            int src_comp, dst_comp;
            int* AMREX_RESTRICT dst_data;
            int* AMREX_RESTRICT src_data;

            // note, in WarpX we only have runtime int data
            static_assert(SrcData::NAI == 0 && DstData::NAI == 0,
                          "SmartCopy assumes all int data is runtime-added.");

            src_comp = m_src_comps_i[j];
            src_data = src.m_runtime_idata[src_comp];

            dst_comp = m_dst_comps_i[j];
            dst_data = dst.m_runtime_idata[dst_comp];

            dst_data[i_dst] = src_data[i_src];
        }
    }
};

/**
 * \brief A factory for creating SmartCopy functors.
 *
 * Given two particle containers, this can create a functor
 * that will perform the smart copy operation between those
 * particle container's tiles.
 */
class SmartCopyFactory
{
    SmartCopyTag m_tag_real;
    SmartCopyTag m_tag_int;
    PolicyVec m_policy_real;
    PolicyVec m_policy_int;
    bool m_defined = false;

public:
    template <class SrcPC, class DstPC>
    SmartCopyFactory (const SrcPC& src, const DstPC& dst) noexcept :
        m_tag_real{getSmartCopyTag(src.getParticleComps(), dst.getParticleComps())},
        m_tag_int{getSmartCopyTag(src.getParticleiComps(), dst.getParticleiComps())},
        m_policy_real{getPolicies(dst.getParticleComps())},
        m_policy_int{getPolicies(dst.getParticleiComps())},
        m_defined{true}
    {}

    [[nodiscard]] SmartCopy getSmartCopy () const noexcept
    {
        AMREX_ASSERT(m_defined);
        return SmartCopy{m_tag_real.size(),
                         m_tag_real.src_comps.dataPtr(),
                         m_tag_real.dst_comps.dataPtr(),
                         m_tag_int.size(),
                         m_tag_int. src_comps.dataPtr(),
                         m_tag_int. dst_comps.dataPtr(),
                         m_policy_real.dataPtr(),
                         m_policy_int.dataPtr()};
    }

    [[nodiscard]] bool isDefined () const noexcept { return m_defined; }
};

#endif //WARPX_SMART_COPY_H_
