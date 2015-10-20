/*
  This file is part of bitplanes.

  bitplanes is free software: you can redistribute it and/or modify
  it under the terms of the Lesser GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  bitplanes is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  Lesser GNU General Public License for more details.

  You should have received a copy of the Lesser GNU General Public License
  along with bitplanes.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BITPLANES_CORE_INTENSITY_TRACKER_H
#define BITPLANES_CORE_INTENSITY_TRACKER_H

#include "bitplanes/core/types.h"
#include "bitplanes/core/algorithm_parameters.h"
#include "bitplanes/core/motion_model.h"
#include "bitplanes/core/internal/channel_data_dense.h"
#include "bitplanes/core/internal/normalization.h"
#include "bitplanes/core/internal/optim_common.h"
#include "bitplanes/core/internal/imwarp.h"
#include "bitplanes/utils/timer.h"
#include "bitplanes/utils/error.h"

#include <opencv2/core.hpp>

#include <limits>
#include <iostream>

namespace bp {


template <class M>
class IntensityTracker
{
 public:
  typedef Matrix33f Transform;
  typedef MotionModel<M> MotionModelType;
  typedef typename MotionModelType::ParameterVector ParameterVector;

 public:
  inline IntensityTracker(AlgorithmParameters p = AlgorithmParameters())
      : _alg_params(p), _T(Matrix33f::Identity()), _T_inv(Matrix33f::Identity()) {}

  inline void setTemplate(const cv::Mat& image, const cv::Rect& bbox)
  {
    setNormalization(bbox);

    _bbox = bbox;
    _cdata.set(image, bbox, _T(0,0), _T_inv(0,2), _T_inv(1,2));
    _hessian = (_cdata.jacobian().transpose() * _cdata.jacobian());

    //_I0 = image(bbox);
  }

  Result track(const cv::Mat& image, const Transform& T_init = Transform::Identity());

 protected:
  AlgorithmParameters _alg_params;
  cv::Rect _bbox;
  ChannelDataDense<M> _cdata;
  cv::Mat _interp_maps[2];
  cv::Mat _Iw, _I0;
  Matrix33f _T, _T_inv;

  typedef typename ChannelDataDense<M>::Hessian Hessian;
  typedef typename ChannelDataDense<M>::Gradient Gradient;

  Hessian _hessian;
  Gradient _gradient;
  Vector_<float> _residuals;

  float linearize(const cv::Mat&, const Transform& T_init);

  void setNormalization(const cv::Rect&)
  {
    _T.setIdentity();
    _T_inv.setIdentity();
  }

  int _interp = cv::INTER_AREA;
}; // IntensityTracker

template<> void IntensityTracker<Homography>::setNormalization(const cv::Rect& bbox)
{
  HartlyNormalization(bbox, _T, _T_inv);
}

template <class M> inline
Result IntensityTracker<M>::track(const cv::Mat& image, const Transform& T_init)
{

  Result ret(T_init);
  Timer timer;

  auto g_norm = this->linearize(image, ret.T);

  const auto p_tol = this->_alg_params.parameter_tolerance,
        f_tol = this->_alg_params.function_tolerance,
        sqrt_eps = std::sqrt(std::numeric_limits<float>::epsilon()),
        tol_opt = 1e-4f * p_tol, rel_factor = std::max(sqrt_eps, g_norm);

  const auto max_iters = this->_alg_params.max_iterations;
  const auto verbose = this->_alg_params.verbose;

  if(g_norm < tol_opt*rel_factor) {
    if(verbose)
      printf("initial value is optimal %g < %g\n", g_norm, tol_opt*rel_factor);

    ret.final_ssd_error = _residuals.squaredNorm();
    ret.first_order_optimality = g_norm;
    ret.time_ms = timer.stop().count();
    ret.num_iterations = 1;
    ret.status = OptimizerStatus::FirstOrderOptimality;
    return ret;
  }

  float old_sum_sq = std::numeric_limits<float>::max();
  bool has_converged = false;
  int it = 1;
  while(!has_converged && it++ < max_iters)
  {
    const ParameterVector dp = MotionModelType::Solve(_hessian, _gradient);
    const auto sum_sq = _residuals.squaredNorm();

    g_norm = _gradient.template lpNorm<Eigen::Infinity>();

    {
      const auto dp_norm = dp.norm();
      const auto p_norm = MotionModelType::MatrixToParams(ret.T).norm();

      if(verbose)
        printf("\t%3d/%d F=%g g=%g |dp|=%0.2e\n",
               it, max_iters, sum_sq, g_norm, dp_norm);

      has_converged = TestConverged(dp_norm, p_norm, p_tol,
                                    g_norm, tol_opt, rel_factor,
                                    sum_sq, old_sum_sq, f_tol,
                                    sqrt_eps, it, max_iters, verbose,
                                    ret.status);
      old_sum_sq = sum_sq;
    }

    const Transform Td = _T_inv * MotionModelType::ParamsToMatrix(dp) * _T;
    ret.T = ret.T * Td;

    if(!has_converged) {
      this->linearize(image, ret.T);
    }
  }

  ret.time_ms = timer.stop().count();
  ret.num_iterations = it;
  ret.final_ssd_error = old_sum_sq;
  ret.first_order_optimality = g_norm;
  if(ret.status == OptimizerStatus::NotStarted)
    ret.status = OptimizerStatus::MaxIterations;

  return ret;
}

template <class M> inline
float IntensityTracker<M>::linearize(const cv::Mat& I, const Transform& T)
{
  imwarp<M>(I, _Iw, T, _bbox, _interp_maps[0], _interp_maps[1], _interp);
  //imwarp<M>(I, _Iw, T, _bbox);
  const auto& I_ref = _cdata.pixels();
  if(I_ref.size() != _residuals.size())
    _residuals.resize( I_ref.size() );

  THROW_ERROR_IF( I.type() != cv::DataType<uint8_t>::type, "uint8_t" );

  const uint8_t* Iw_ptr = _Iw.ptr<const uint8_t>();
  for(int y = 0, i = 0; y < _bbox.height; ++y)
  {
    for(int x = 0; x < _bbox.width; ++x, ++i)
    {
      int ii = y*_Iw.cols + x;
      _residuals[i] = static_cast<float>(Iw_ptr[ii]) - I_ref[i];
    }
  }

  /*
  cv::Mat diff;
  cv::absdiff(_I0, _Iw, diff);
  cv::imshow("diff", diff);
  cv::waitKey(10);
  */

  _gradient = _cdata.jacobian().transpose() * _residuals;
  return _gradient.template lpNorm<Eigen::Infinity>();
}

}; // bp

#endif // BITPLANES_CORE_INTENSITY_TRACKER_H