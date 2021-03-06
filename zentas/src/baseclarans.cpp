// Copyright (c) 2016 Idiap Research Institute, http://www.idiap.ch/
// Written by James Newling <jnewling@idiap.ch>

#include <zentas/baseclarans.hpp>

namespace nszen
{

void BaseClarans::reset_sample_custom(size_t              k,
                                      size_t              j,
                                      size_t              nearest_center,
                                      const double* const distances)
{
  reset_sample_nearest_2_infos_margin(k, j, nearest_center, distances);
}

void ClaransStatistics::set_to_zero()
{
  M      = 0;
  M_star = 0;
  R1     = 0;
  R2     = 0;
  m      = 0;
  m_star = 0;
}

void ClaransStatistics::increment(double d_first_nearest,
                                  double e_first_nearest,
                                  double d_second_nearest,
                                  double e_second_nearest)
{
  M = std::max(M, e_second_nearest - e_first_nearest);
  M_star += (e_second_nearest - e_first_nearest);
  R1 = std::max(R1, d_first_nearest);
  R2 = std::max(R2, d_second_nearest);
}

void ClaransStatistics::set_normalised_statistics(size_t ndata)
{
  m      = 0.0;
  m_star = 0.0;
  if (ndata > 0)
  {
    m      = M / static_cast<double>(ndata);
    m_star = M_star / static_cast<double>(ndata);
  }
}

// template <class TMetric, class TData>
double BaseClarans::get_delta_hat_l3(
  size_t k1, size_t k2, size_t j2, double d_nearest_k1, const double* const cc)
{

  /* the number of samples used will
   * be n_full_knowledge * 2 ^ ( { -L+1, -L+2, ..0} ).
   * so L = 1 : (1), L = 3 : (1/4, 1/2, 1) etc.
   * The smallest will 2^(1-L) of the total
   * At most a fraction 2^(1-L) of good proposals will be rejected. */

  double n_per_cluster               = static_cast<double>(ndata) / static_cast<double>(K);
  double target_min_mean_per_cluster = 24.0;

  /* choose largest L :  n_per_cluster * 2^(1 - L) > target_min_mean_per_cluster
   * 2^(1 - L) > target_min_mean_per_cluster / n_per_cluster
   * 1 - L > log_2 (target_min_mean_per_cluster / n_per_cluster)
   * 1 - log_2 (target_min_mean_per_cluster / n_per_cluster) > L
   * L = floor (1 - log_2 (target_min_mean_per_cluster / n_per_cluster))
   * */

  size_t L;
  if (n_per_cluster <= target_min_mean_per_cluster)
  {
    L = 1;
  }
  else
  {
    L =
      static_cast<size_t>(std::floor(1. - std::log2(target_min_mean_per_cluster / n_per_cluster)));
  }

  /* some workers  */
  double dist_k_j2;

  size_t random_index;
  std::vector<std::pair<size_t, size_t>> start_end;
  double adist;
  double n_samples_total_fl;
  double delta_hat = 0.;
  size_t n_full_knowledge;
  // double cluster_Z;
  size_t n_nk1;

  /* These vectors keep vital statistics for non-eliminated (ndet_x) clusters.
   * Perhaps they should be packaged together for memory reasons. */
  /* the cluster indices of non-eliminated clusters */
  std::vector<size_t> ndet_k;
  /* the distance from cluster to proposed replacer */
  std::vector<double> ndet_dists_k_j2;
  /* ndata of the cluster */
  std::vector<size_t> ndet_ndatas;
  /* the starting index of random sample, will be contiguous from this point on */
  std::vector<size_t> ndet_random_phase_inds;
  /* starting sample for a given l (1, ... ,L) */
  std::vector<size_t> ndet_starting_inds;
  /* ending sample for a given l (1, ... ,L) */
  std::vector<size_t> ndet_final_inds;
  /* total number sampled for a given l */
  std::vector<size_t> ndet_n_active;
  /* total number sampled at l - 1 */
  std::vector<size_t> ndet_n_active_old;
  /* sum of the samples */
  std::vector<double> ndet_sum_delta_i;

  /* determine which of the (non-k1) clusters are non-eliminated, using standard l2 triangle
   * inequality test */
  n_nk1 = 0;
  for (size_t k = 0; k < K; ++k)
  {
    if ((k != k1) && (cc[k2 * K + k] - 2 * cluster_statistics[k].R1 < get_d1(k2, j2)))
    {
      set_center_sample_distance(k, k2, j2, cc[k + K * k2] + get_d1(k2, j2), dist_k_j2);
      if (0.5 * dist_k_j2 < cluster_statistics[k].R1)
      {
        ndet_k.push_back(k);
        ndet_dists_k_j2.push_back(dist_k_j2);
        ++n_nk1;
      }
    }
  }

  /* determine whether k1 is eliminated */
  set_center_sample_distance(k1, k2, j2, cc[k1 + K * k2] + get_d1(k2, j2), dist_k_j2);
  double delta_E_known_base = f_energy(std::min(d_nearest_k1, dist_k_j2));
  if (cluster_statistics[k1].R1 + cluster_statistics[k1].R2 <= dist_k_j2)
  {
    delta_E_known_base += cluster_statistics[k1].M_star;
  }
  else
  {
    ndet_k.push_back(k1);
    ndet_dists_k_j2.push_back(dist_k_j2);
  }

  /* populating the stats vectors */
  n_full_knowledge = 0;
  // cluster_Z = 0;
  for (auto& k : ndet_k)
  {
    ndet_ndatas.push_back(get_ndata(k));
    n_full_knowledge += get_ndata(k);
    random_index = dis(gen) % get_ndata(k);
    ndet_random_phase_inds.push_back(random_index);
    ndet_starting_inds.push_back(random_index);
    ndet_final_inds.push_back(random_index);
    ndet_n_active.push_back(0);
    ndet_n_active_old.push_back(0);
    ndet_sum_delta_i.push_back(0);
  }

  /* computing delta_hat  */
  for (size_t l = 0; l < L; ++l)
  {
    n_samples_total_fl =
      static_cast<double>(n_full_knowledge) * std::pow<double>(2, -static_cast<double>(L - 1 - l));
    delta_hat = delta_E_known_base;

    /* updating stats : how many samples to process at this level l */
    for (size_t ki = 0; ki < ndet_k.size(); ++ki)
    {
      ndet_n_active_old[ki] = ndet_n_active[ki];

      /* unless this is the final level, distribute the n_samples_total_fl among the k clusters,
       * proportional to (number in cluster). */
      if (l != L - 1)
      {
        ndet_n_active[ki] =
          std::min(ndet_ndatas[ki],
                   1 + static_cast<size_t>(std::ceil(n_samples_total_fl *
                                                     static_cast<double>(ndet_ndatas[ki]) /
                                                     static_cast<double>(ndata))));
      }

      /* if this is the final level, process all samples */
      else
      {
        ndet_n_active[ki] = ndet_ndatas[ki];
      }

      ndet_starting_inds[ki] =
        (ndet_random_phase_inds[ki] + ndet_n_active_old[ki]) % ndet_ndatas[ki];
      ndet_final_inds[ki] = (ndet_random_phase_inds[ki] + ndet_n_active[ki]) % ndet_ndatas[ki];
    }

    /* updating delta_hats */
    for (size_t ki = 0; ki < ndet_k.size(); ++ki)
    {
      /* if ndata = nactive, this is the first time, so we let it through */
      if (ndet_n_active_old[ki] < ndet_ndatas[ki])
      {
        /* unpack for convenience  */
        size_t k = ndet_k[ki];
        // double
        dist_k_j2      = ndet_dists_k_j2[ki];
        size_t s_index = ndet_starting_inds[ki];
        size_t f_index = ndet_final_inds[ki];

        /* determine which samples to compute */
        start_end.clear();
        /* (1) this is the case where all ndata samples are processed in one go */
        if (ndet_n_active_old[ki] == 0 && ndet_n_active[ki] == get_ndata(k))
        {
          if (s_index != f_index)
          {
            throw zentas::zentas_error(
              "this seems wrong (s_index != f_index). Come and check it out");
          }
          start_end.emplace_back(0, get_ndata(k));
        }
        /* (2) the case where you need to tail back to the start */
        else if (s_index > f_index)
        {
          start_end.emplace_back(s_index, get_ndata(k));
          start_end.emplace_back(0, f_index);
        }

        /* (3) the simple case of contiguous samples */
        else
        {
          start_end.emplace_back(s_index, f_index);
        }

        /* split here on k1 vs !k1. !k1 are the first n_nk1 */
        /* first case : !k1 */
        if (ki < n_nk1)
        {
          for (auto& se : start_end)
          {
            for (size_t j = se.first; j < se.second; ++j)
            {
              if (0.5 * dist_k_j2 < get_d1(k, j))
              {
                set_sample_sample_distance(k2, j2, k, j, get_d1(k, j), adist);
                if (adist < get_d1(k, j))
                {
                  ndet_sum_delta_i[ki] += (f_energy(adist) - get_e1(k, j));
                }
              }
            }
          }
        }

        /* k1 */
        else
        {
          for (auto& se : start_end)
          {
            for (size_t j = se.first; j < se.second; ++j)
            {
              if (get_d1(k1, j) + get_d2(k1, j) <= dist_k_j2)
              {
                ndet_sum_delta_i[ki] += energy_margins[k1][j];
              }
              else
              {
                set_sample_sample_distance(k1, j, k2, j2, get_d2(k1, j), adist);
                if (adist < get_d2(k1, j))
                {
                  ndet_sum_delta_i[ki] += f_energy(adist) - get_e1(k1, j);
                }
                else
                {
                  ndet_sum_delta_i[ki] += energy_margins[k1][j];
                }
              }
            }
          }
        }
      }

      delta_hat += ndet_sum_delta_i[ki] * static_cast<double>(ndet_ndatas[ki]) /
                   static_cast<double>(ndet_n_active[ki]);

    }  // end with all clusters

    if (delta_hat >= 0.)
    {
      return std::numeric_limits<double>::max();  // delta_hat;
    }
  }  // end of l

  return delta_hat;
}

double BaseClarans::get_delta_E_hoeffding_l3(
  size_t k1, size_t k2, size_t j2, double d_nearest_k1, const double* const cc)
{

  /* the number of samples used will be n_full_knowledge*2^( {1-L, 2-L, ..L-L} ) */
  double n_per_cluster        = static_cast<double>(ndata) / static_cast<double>(K);
  double target_n_per_cluster = 200.0;
  size_t L = static_cast<size_t>(std::ceil(std::log2(n_per_cluster / target_n_per_cluster)));

  /* if Delta < 0 :  P(proposal accepted) > p_null_accept_lb ==> P(proposal rejected in round) <
   * p_null_round_reject_ub. */
  double p_null_accept_lb       = 0.01;
  double p_null_round_accept_lb = std::pow(p_null_accept_lb, 1. / static_cast<double>(L - 1));
  double p_null_round_reject_ub = 1 - p_null_round_accept_lb;

  /* some workers  */
  double dist_k_j2{0};
  size_t random_index{0};
  std::vector<std::pair<size_t, size_t>> start_end;
  double cluster_nM{0};
  double cluster_pM{0};
  double adist{0};
  double n_samples_total_fl{0};
  double delta_hat{0};
  size_t n_full_knowledge{0};
  double cluster_Z{0};
  size_t n_nk1{0};

  /* These vectors keep vital statistics for non-eliminated (ndet_x) clusters.
   * Perhaps they should be packaged together for memory reasons. */
  /* the cluster indices of non-eliminated clusters */
  std::vector<size_t> ndet_k;
  /* the distance from cluster to proposed replacer */
  std::vector<double> ndet_dists_k_j2;
  /* ndata of the cluster */
  std::vector<size_t> ndet_ndatas;
  /* the starting index of random sample, will be contiguous from this point on */
  std::vector<size_t> ndet_random_phase_inds;
  /* starting sample for a given l (1, ... ,L) */
  std::vector<size_t> ndet_starting_inds;
  /* ending sample for a given l (1, ... ,L) */
  std::vector<size_t> ndet_final_inds;
  /* total number sampled for a given l */
  std::vector<size_t> ndet_n_active;
  /* total number sampled at l - 1 */
  std::vector<size_t> ndet_n_active_old;
  /* how many of those sampled are non-zero */
  std::vector<size_t> ndet_n_non_zero;
  /* sum of the samples */
  std::vector<double> ndet_sum_delta_i;
  /* the margin multiplied by the total number in the cluster */
  std::vector<double> ndet_cluster_nMs;
  /* the margin multiplied by (the total number in cluster)/(total number), all squared. */
  std::vector<double> ndet_pMs_squared;

  /* determine which of the (non-k1) clusters are non-eliminated, using standard l2 triangle
   * inequality test */

  n_nk1 = 0;
  for (size_t k = 0; k < K; ++k)
  {
    if ((k != k1) && (cc[k2 * K + k] - 2 * cluster_statistics[k].R1 < get_d1(k2, j2)))
    {
      set_center_sample_distance(k, k2, j2, cc[k + K * k2] + get_d2(k2, j2), dist_k_j2);
      if (0.5 * dist_k_j2 < cluster_statistics[k].R1)
      {
        ndet_k.push_back(k);
        ndet_dists_k_j2.push_back(dist_k_j2);
        ++n_nk1;
      }
    }
  }

  /* determine whether k1 is eliminated */
  set_center_sample_distance(k1, k2, j2, cc[k1 + K * k2] + get_d1(k2, j2), dist_k_j2);
  double delta_E_known_base = f_energy(std::min(d_nearest_k1, dist_k_j2));
  if (cluster_statistics[k1].R1 + cluster_statistics[k1].R2 <= dist_k_j2)
  {
    delta_E_known_base += cluster_statistics[k1].M_star;
  }
  else
  {
    ndet_k.push_back(k1);
    ndet_dists_k_j2.push_back(dist_k_j2);
  }

  /* populating the stats vectors */
  n_full_knowledge = 0;
  cluster_Z        = 0;
  for (auto& k : ndet_k)
  {
    ndet_ndatas.push_back(get_ndata(k));
    n_full_knowledge += get_ndata(k);
    random_index = dis(gen) % get_ndata(k);
    ndet_random_phase_inds.push_back(random_index);
    ndet_starting_inds.push_back(random_index);
    ndet_final_inds.push_back(random_index);
    ndet_n_active.push_back(0);
    ndet_n_non_zero.push_back(0);
    ndet_n_active_old.push_back(0);
    ndet_sum_delta_i.push_back(0);
    cluster_nM = static_cast<double>(get_ndata(k)) * cluster_statistics[k].M;
    cluster_pM = cluster_nM / static_cast<double>(ndata);
    ndet_cluster_nMs.push_back(cluster_nM);
    ndet_pMs_squared.push_back(cluster_pM * cluster_pM);
    cluster_Z += cluster_nM;
  }
  for (auto& w : ndet_cluster_nMs)
  {
    w /= cluster_Z;
  }

  /* computing delta_hat and determining whether it is high enough to abort */
  for (size_t l = 0; l < L; ++l)
  {
    n_samples_total_fl = static_cast<double>(n_full_knowledge) *
                         std::pow<double>(2, static_cast<double>(l) - static_cast<double>(L) + 1.);
    delta_hat = delta_E_known_base;

    /* updating stats : how many samples to process at this level l */
    for (size_t ki = 0; ki < ndet_k.size(); ++ki)
    {
      ndet_n_active_old[ki] = ndet_n_active[ki];

      /* unless this is the final level, distribute the n_samples_total_fl among the k clusters,
       * proportional to (number in cluster)*(margin of cluster). If the assigned amount of data to
       * a cluster exceeds the total number in the cluster, simply use the full cluster : the total
       * amount of data used may be less than n_samples_total_fl */
      if (l != L - 1)
      {
        ndet_n_active[ki] =
          std::min(ndet_ndatas[ki],
                   1 + static_cast<size_t>(std::ceil(n_samples_total_fl * ndet_cluster_nMs[ki])));
      }

      /* if this is the final level, process all samples */
      else
      {
        ndet_n_active[ki] = ndet_ndatas[ki];
      }

      ndet_starting_inds[ki] =
        (ndet_random_phase_inds[ki] + ndet_n_active_old[ki]) % ndet_ndatas[ki];
      ndet_final_inds[ki] = (ndet_random_phase_inds[ki] + ndet_n_active[ki]) % ndet_ndatas[ki];
    }

    /* updating delta_hats */
    for (size_t ki = 0; ki < ndet_k.size(); ++ki)
    {
      /* if ndata = nactive, this is the first time, so we let it through */
      if (ndet_n_active_old[ki] < ndet_ndatas[ki])
      {
        /* unpack for convenience  */
        size_t k = ndet_k[ki];
        // double
        dist_k_j2      = ndet_dists_k_j2[ki];
        size_t s_index = ndet_starting_inds[ki];
        size_t f_index = ndet_final_inds[ki];

        /* determine which samples to compute */
        start_end.clear();
        /* (1) this is the case where all ndata samples are processed in one go */
        if (ndet_n_active_old[ki] == 0 && ndet_n_active[ki] == get_ndata(k))
        {
          if (s_index != f_index)
          {
            throw zentas::zentas_error(
              "this seems wrong (s_index != f_index). Come and check it out");
          }
          start_end.emplace_back(0, get_ndata(k));
        }
        /* (2) the case where you need to tail back to the start */
        else if (s_index > f_index)
        {
          start_end.emplace_back(s_index, get_ndata(k));
          start_end.emplace_back(0, f_index);
        }

        /* (3) the simple case of contiguous samples */
        else
        {
          start_end.emplace_back(s_index, f_index);
        }

        /* split here on k1 vs !k1. !k1 are the first n_nk1 */
        /* first case : !k1 */
        if (ki < n_nk1)
        {
          for (auto& se : start_end)
          {
            for (size_t j = se.first; j < se.second; ++j)
            {
              if (0.5 * dist_k_j2 < get_d1(k, j))
              {
                set_sample_sample_distance(k2, j2, k, j, get_d1(k, j), adist);
                if (adist < get_d1(k, j))
                {
                  ndet_sum_delta_i[ki] += (f_energy(adist) - get_e1(k, j));
                  ++ndet_n_non_zero[ki];
                }
              }
            }
          }
        }

        /* k1 */
        else
        {
          for (auto& se : start_end)
          {
            for (size_t j = se.first; j < se.second; ++j)
            {
              ++ndet_n_non_zero[ki];
              if (get_d1(k1, j) + get_d2(k1, j) <= dist_k_j2)
              {
                ndet_sum_delta_i[ki] += energy_margins[k1][j];
              }
              else
              {
                set_sample_sample_distance(k1, j, k2, j2, get_d2(k1, j), adist);
                if (adist < get_d2(k1, j))
                {
                  ndet_sum_delta_i[ki] += f_energy(adist) - get_e1(k1, j);
                }
                else
                {
                  ndet_sum_delta_i[ki] += energy_margins[k1][j];
                }
              }
            }
          }
        }
      }

      delta_hat += ndet_sum_delta_i[ki] * static_cast<double>(ndet_ndatas[ki]) /
                   static_cast<double>(ndet_n_active[ki]);

    }  // end with all clusters

    /* the sum used in the hoeffding bound. Consider,
     * Delta < 0 =>
     * P( hat Delta / N - Delta / N > tau ) <= P ( hat Delta / N - xi / N > tau/2 ) + P ( xi / N -
     * Delta / N > tau/2 )
     * (see manuscript for defn of xi).
     * Assuming that P ( xi / N - Delta / N > tau/2 ) <= P ( hat Delta / N - xi / N > tau/2 )
     * (which is not a bad assumption,
     * see manuscript to see that they have the same distribution), we have
     * <= 2 exp [ {- tau ^ 2 / 4} / {sum_K ... see manuscript } ]
     *
     * */
    double smirnoff_sum = 0;
    double ndet_n_active_squared;
    for (size_t ki = 0; ki < ndet_ndatas.size(); ++ki)
    {
      if (ndet_n_active[ki] != ndet_ndatas[ki])
      {
        ndet_n_active_squared =
          static_cast<double>(ndet_n_active[ki]) * static_cast<double>(ndet_n_active[ki]);
        smirnoff_sum += (ndet_pMs_squared[ki] * static_cast<double>(ndet_n_non_zero[ki])) /
                        ndet_n_active_squared;
      }
    }
    double threshold = 2 * std::sqrt(std::log(2. / p_null_round_reject_ub) * smirnoff_sum);

    if (delta_hat / static_cast<double>(ndata) > threshold)
    {
      return std::numeric_limits<double>::max();
    }
  }  // end of l

  return delta_hat;
}

bool BaseClarans::update_centers_greedy()
{

  bool accept = false;

  // the proposal is : make the j_2'th of cluster p_2 the center of cluster k_1
  size_t k1{0};
  size_t k2{0};
  size_t j2{0};

  n_proposals = 0;

  double delta_E;
  while (accept == false && n_proposals < max_proposals && get_time_remaining() > 0)
  {

    set_proposal(k1, k2, j2);
    ++n_proposals;
    delta_E = get_delta_E(k1, k2, j2, false);
    accept  = (delta_E < 0);
  }

  if (accept)
  {
    acceptance_call(k1, k2, j2);
  }

  return accept;
}

bool BaseClarans::update_centers_patient()
{

  n_proposals = 0;
  std::mutex               mutex_uc3;
  std::vector<std::thread> threads;

  // global variables
  size_t best_k1{0};
  size_t best_k2{0};
  size_t best_j2{0};
  double best_delta_E = std::numeric_limits<double>::max();

  size_t time_limit = 0;

  if (get_time_in_update_centers() < get_time_in_update_sample_info())
  {
    time_limit = get_time_in_update_sample_info() - get_time_in_update_centers();
  }

  std::chrono::time_point<std::chrono::high_resolution_clock> t0 =
    std::chrono::high_resolution_clock::now();

  for (size_t ti = 0; ti < get_nthreads(); ++ti)
  {
    threads.emplace_back(
      [this, &mutex_uc3, &best_k1, &best_k2, &best_j2, &best_delta_E, time_limit, t0]() {

        size_t n_proposals_local = 0;
        size_t k1;
        size_t k2;
        size_t j2;

        std::chrono::time_point<std::chrono::high_resolution_clock> t1;
        size_t                                                      time_in_update_centers = 0;

        t1                     = std::chrono::high_resolution_clock::now();
        time_in_update_centers = static_cast<size_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

        while (
          (n_proposals_local == 0) || (time_in_update_centers < time_limit) ||
          (best_delta_E >= 0 && n_proposals < get_max_proposals() && get_time_remaining() > 0))
        {

          mutex_uc3.lock();
          set_proposal(k1, k2, j2);
          mutex_uc3.unlock();

          double delta_E = get_delta_E(k1, k2, j2, true);

          if (delta_E < 0)
          {
            std::lock_guard<std::mutex> lock(mutex_uc3);
            if (delta_E < best_delta_E)
            {
              best_k1      = k1;
              best_k2      = k2;
              best_j2      = j2;
              best_delta_E = delta_E;
            }
          }

          ++n_proposals_local;

          t1                     = std::chrono::high_resolution_clock::now();
          time_in_update_centers = static_cast<size_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

          std::lock_guard<std::mutex> lock(mutex_uc3);
          ++n_proposals;
        }

      });
  }

  for (auto& t : threads)
  {
    t.join();
  }

  bool accept;
  if (best_delta_E < 0)
  {
    acceptance_call(best_k1, best_k2, best_j2);
    accept = true;
  }

  else
  {
    accept = false;
  }

  return accept;
}

void BaseClarans::initialise_with_kmeanspp() { default_initialise_with_kmeanspp(); }

void BaseClarans::set_redistribute_order(std::vector<size_t>& redistribute_order)
{
  redistribute_order[0] = k_to;
  size_t position       = 1;
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k_to)
    {
      redistribute_order[position] = k;
      ++position;
    }
  }
}

bool BaseClarans::update_centers()
{
  if (patient == true)
  {
    return update_centers_patient();
  }

  else
  {
    return update_centers_greedy();
  }
}

void BaseClarans::reset_1_nearest_excluding(XNearestInfo&       nearest_info,
                                            size_t              k,
                                            const double* const distances)
{
  double distance_nearest = std::numeric_limits<double>::max();
  size_t k_nearest        = 0;
  for (size_t kp = 0; kp < K; ++kp)
  {
    if (kp != k)
    {
      if (distances[kp] < distance_nearest)
      {
        distance_nearest = distances[kp];
        k_nearest        = kp;
      }
    }
  }
  nearest_info.reset(k_nearest, distance_nearest, f_energy(distance_nearest));
}

void BaseClarans::set_center_center_distances(size_t k, double* const distances)
{
  for (size_t kp = 0; kp < K; ++kp)
  {
    set_center_center_distance_nothreshold(k, kp, distances[kp]);
  }
}

double BaseClarans::get_delta_E_not_k1_l2(
  size_t k1, size_t k2, size_t j2, const double* const cc, bool serial)
{

  size_t nthreads;
  if (serial == true)
  {
    nthreads = 1;
  }
  else
  {
    nthreads = get_nthreads();
  }

  std::vector<double> dists_k_j2(K);

  // size_t n_elims = 0;

  std::vector<size_t> non_eliminated;
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k1)
    {
      // if (cc[k2*K + k] - 2*cluster_statistics[k].R1 < get_d1(k2, j2)){
      if (cc[k2 * K + k] - get_d1(k2, j2) < 2 * cluster_statistics[k].R1)
      {
        set_center_sample_distance(k, k2, j2, cc[k2 * K + k] + get_d1(k2, j2), dists_k_j2[k]);
        if (0.5 * dists_k_j2[k] < cluster_statistics[k].R1)
        {
          non_eliminated.push_back(k);
        }
      }
    }
  }

  std::vector<std::thread> threads;
  std::vector<double>      thread_delta_E_not_k1(nthreads, 0);

  for (size_t ti = 0; ti < nthreads; ++ti)
  {
    threads.emplace_back(
      [this, &non_eliminated, ti, k2, j2, &dists_k_j2, &thread_delta_E_not_k1, nthreads]() {
        double adist;
        for (auto& k : non_eliminated)
        {
          for (size_t j = get_start(ti, nthreads, 0, get_ndata(k));
               j < get_end(ti, nthreads, 0, get_ndata(k));
               ++j)
          {
            if (0.5 * dists_k_j2[k] < get_d1(k, j))
            {
              set_sample_sample_distance(k2, j2, k, j, get_d1(k, j), adist);
              if (adist < get_d1(k, j))
              {
                thread_delta_E_not_k1[ti] += (f_energy(adist) - get_e1(k, j));
              }
            }
          }
        }
      });
  }

  for (auto& t : threads)
  {
    t.join();
  }

  double delta_E_not_k1 = 0;
  for (auto& x : thread_delta_E_not_k1)
  {
    delta_E_not_k1 += x;
  }

  return delta_E_not_k1;
}

void BaseClarans::update_k_to_sample_info_l2(size_t              j_a,
                                             size_t              j_z,
                                             const double* const dists_centers_old_k_to,
                                             const double* const cc)
{
  double adistance;
  size_t a1;
  size_t a2;
  double d1;
  double d2;
  for (size_t j = j_a; j < j_z; ++j)
  {
    set_center_sample_distance(
      k_to, k_to, j, dists_centers_old_k_to[k_to] + get_d1(k_to, j), adistance);
    a1 = k_to;
    d1 = adistance;
    a2 = get_a2(k_to, j);
    d2 = get_d2(k_to, j);
    set_nearest_12_warmstart(k_to, j, a1, a2, d1, d2, cc);
    reset_sample_info_direct(k_to, j, a1, a2, d1, d2);
  }
}

void BaseClarans::pll_update_sample_info_l23(size_t              k,
                                             size_t              j_a,
                                             size_t              j_z,
                                             const double* const dists_centers_min_pr,
                                             const double* const cc)
{

  double adistance;
  size_t a1;
  size_t a2;
  double d1;
  double d2;

  for (size_t j = j_a; j < j_z; ++j)
  {
    if (dists_centers_min_pr[k] <= get_d1(k, j) + get_d2(k, j))
    {
      set_center_sample_distance(k_to, k, j, cc[k + K * k_to] + get_d1(k, j), adistance);
      if (get_a2(k, j) == k_to)
      {
        a2 = k_to;
        d2 = adistance;
        a1 = k;
        d1 = get_d1(k, j);

        set_nearest_12_warmstart(k, j, a1, a2, d1, d2, cc);
        reset_sample_info_direct(k, j, a1, a2, d1, d2);
        // reset_sample_infos(k,j);
      }
      else
      {
        if (adistance < get_d1(k, j))
        {
          reset_second_nearest_info(k, j, get_a1(k, j), get_d1(k, j), get_e1(k, j));
          reset_nearest_info(k, j, k_to, adistance, f_energy(adistance));
          refresh_energy_margins(k, j);
        }
        else if (adistance < get_d2(k, j))
        {
          reset_second_nearest_info(k, j, k_to, adistance, f_energy(adistance));
          refresh_energy_margins(k, j);
        }
      }
    }
  }
}

/* Determine the nearest and second nearest of sample j1 of cluster k1, given that distance to
 * center a1 is d1 and distance to a2 is d2. It is not necessary that d1 < d2 at entry. */
void BaseClarans::set_nearest_12_warmstart(
  size_t k1, size_t j1, size_t& a1, size_t& a2, double& d1, double& d2, const double* const cc)
{

  double adist;

  const size_t a1_start = a1;
  const size_t a2_start = a2;

  // make d1 <= d2
  if (d1 > d2)
  {
    std::swap(a1, a2);
    std::swap(d1, d2);
  }

  // iterate.
  for (size_t k = 0; k < K; ++k)
  {
    if (k != a1_start && k != a2_start)
    {

      // simple triangle inequality test, if succesful we know that k is not a2 (or a1).
      if (std::max(cc[k * K + a1] - d1, cc[k * K + a2] - d2) < d2)
      {
        set_center_sample_distance(k, k1, j1, d2, adist);
        // set_center_sample_distance(k, k1, j1, std::min(cc[k*K + k1] + get_d1(k1, j1), d2),
        // adist);

        if (adist < d2)
        {
          if (adist < d1)
          {
            a2 = a1;
            d2 = d1;

            a1 = k;
            d1 = adist;
          }
          else
          {
            a2 = k;
            d2 = adist;
          }
        }
      }
    }
  }
}

void BaseClarans::reset_sample_info_direct(
  size_t k, size_t j, size_t a1, size_t a2, double d1, double d2)
{
  reset_nearest_info(k, j, a1, d1, f_energy(d1));
  reset_second_nearest_info(k, j, a2, d2, f_energy(d2));
  refresh_energy_margins(k, j);
}

double
BaseClarans::get_delta_E_k1_l12(size_t k1, size_t k2, size_t j2, double dist_k1_j2, bool serial)
{

  size_t nthreads;
  if (serial == true)
  {
    nthreads = 1;
  }
  else
  {
    nthreads = get_nthreads();
  }

  double delta_E_k1;

  /* in this case, the proposed center (k2, j2) is really far from cluster k1:
   * for all datapoints, there's a center, other than k1,
   * at least as close as (k2, j2) */
  if (cluster_statistics[k1].R1 + cluster_statistics[k1].R2 <= dist_k1_j2)
  {
    delta_E_k1 = cluster_statistics[k1].M_star;
  }

  else
  {
    delta_E_k1 = 0;

    std::vector<std::thread> threads;
    std::vector<double>      thread_delta_E_k1(nthreads);

    for (size_t ti = 0; ti < nthreads; ++ti)
    {

      threads.emplace_back([this, ti, &thread_delta_E_k1, k1, k2, j2, dist_k1_j2, nthreads]() {
        double adistance;
        for (size_t j = get_start(ti, nthreads, 0, get_ndata(k1));
             j < get_end(ti, nthreads, 0, get_ndata(k1));
             ++j)
        {
          /* the proposed center (k2, j2) is defos at least as far as the second nearest */
          if (get_d1(k1, j) + get_d2(k1, j) <= dist_k1_j2)
          {
            thread_delta_E_k1[ti] += energy_margins[k1][j];
          }
          else
          {
            set_sample_sample_distance(k1, j, k2, j2, get_d2(k1, j), adistance);
            /* nearest excluding k1 is not (k2, j2) */
            if (adistance > get_d2(k1, j))
            {
              thread_delta_E_k1[ti] += energy_margins[k1][j];
            }

            /* nearest excluding k1 is (k2, j2) */
            else
            {
              thread_delta_E_k1[ti] += f_energy(adistance) - get_e1(k1, j);
            }
          }
        }
      });
    }

    for (auto& t : threads)
    {
      t.join();
    }

    for (auto& x : thread_delta_E_k1)
    {
      delta_E_k1 += x;
    }
  }

  return delta_E_k1;
}

double BaseClarans::get_delta_E_not_k1_l1(
  size_t k1, size_t k2, size_t j2, const double* const dists_centers_j2, bool serial)
{

  size_t nthreads;
  if (serial == true)
  {
    nthreads = 1;
  }
  else
  {
    nthreads = get_nthreads();
  }

  std::vector<size_t> non_eliminated;
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k1)
    {
      if (0.5 * dists_centers_j2[k] < cluster_statistics[k].R1)
      {
        non_eliminated.push_back(k);
      }
    }
  }

  std::vector<std::thread> threads;
  std::vector<double>      thread_delta_E_not_k1(nthreads, 0);
  for (size_t ti = 0; ti < nthreads; ++ti)
  {
    threads.emplace_back([this,
                          ti,
                          &non_eliminated,
                          dists_centers_j2,
                          k1,
                          k2,
                          j2,
                          &thread_delta_E_not_k1,
                          nthreads]() {
      double adist;
      for (auto& k : non_eliminated)
      {
        for (size_t j = get_start(ti, nthreads, 0, get_ndata(k));
             j < get_end(ti, nthreads, 0, get_ndata(k));
             ++j)
        {
          if (0.5 * dists_centers_j2[k] < get_d1(k, j))
          {
            set_sample_sample_distance(k2, j2, k, j, get_d1(k, j), adist);
            if (adist < get_d1(k, j))
            {
              thread_delta_E_not_k1[ti] += (f_energy(adist) - get_e1(k, j));
            }
          }
        }
      }
    });
  }

  for (auto& t : threads)
  {
    t.join();
  }

  double delta_E_not_k1 = 0;
  for (auto& x : thread_delta_E_not_k1)
  {
    delta_E_not_k1 += x;
  }

  return delta_E_not_k1;
}

void BaseClarans::acceptance_call(size_t k1, size_t k2, size_t j2)
{
  k_to   = k1;
  k_from = k2;
  j_from = j2;

  custom_acceptance_call();

  // manoeuvre,
  move_center_into_its_own_cluster(k_to);
  overwrite_center_with_sample(k_to, k_from, j_from);
  remove_with_tail_pull(k_from, j_from);
  // jn, june 2017: it looks like the old center is now on the tail. is this not violating
  // randomness with cluster?
}

std::string BaseClarans::get_round_summary()
{
  std::stringstream ss;
  ss << get_base_summary_string() << "nprops=" << n_proposals;
  return ss.str();
}

void BaseClarans::reset_second_nearest_info(
  size_t k, size_t j, size_t k_second_nearest, double d_second_nearest, double e_second_nearest)
{
  if (nearest_2_infos[k][j].a_x != k_second_nearest ||
      nearest_2_infos[k][j].d_x != d_second_nearest)
  {
    signal_cluster_change(k);
  }
  nearest_2_infos[k][j].a_x = k_second_nearest;
  nearest_2_infos[k][j].d_x = d_second_nearest;
  nearest_2_infos[k][j].e_x = e_second_nearest;
}

void BaseClarans::set_proposal(size_t& k1, size_t& k2, size_t& j2)
{
  k1 = draw_k_uniform();
  k2 = draw_k_prop_ndata();
  j2 = draw_j_uniform(k2);
}

void BaseClarans::update_k_to_k_from_j_from(size_t k_to_in, size_t k_from_in, size_t j_from_in)
{
  k_to   = k_to_in;
  k_from = k_from_in;
  j_from = j_from_in;
}

void BaseClarans::pll_update_sample_info_l1(size_t k,
                                            size_t j_a,
                                            size_t j_z,
                                            double dists_centers_min_pr_k)
{
  double adistance;
  for (size_t j = j_a; j < j_z; ++j)
  {
    /* a TEST74 (grep) */
    if (dists_centers_min_pr_k <= get_d1(k, j) + get_d2(k, j))
    {
      if (get_a2(k, j) == k_to)
      {
        reset_sample_infos(k, j);
      }
      else
      {
        set_center_sample_distance(k_to, k, j, get_d2(k, j), adistance);
        if (adistance < get_d1(k, j))
        {
          reset_second_nearest_info(k, j, get_a1(k, j), get_d1(k, j), get_e1(k, j));
          reset_nearest_info(k, j, k_to, adistance, f_energy(adistance));
          refresh_energy_margins(k, j);
        }
        else if (adistance < get_d2(k, j))
        {
          reset_second_nearest_info(k, j, k_to, adistance, f_energy(adistance));
          refresh_energy_margins(k, j);
        }
      }
    }
  }
}

/* ************************************
 * *********** tests ******************
 * ********************************** */

void BaseClarans::custom_ndata_test()
{

  for (size_t k = 0; k < K; ++k)
  {
    if (energy_margins[k].size() != get_ndata(k))
    {
      mowri << "k = " << k << zentas::Endl;
      mowri << "size of energy_margins[k] " << energy_margins[k].size() << zentas::Endl;
      mowri << "size of cluster_datas[k] " << get_ndata(k) << zentas::Endl;
      mowri << "size of nearest_2_infos[k] " << nearest_2_infos[k].size() << zentas::Endl;

      throw zentas::zentas_error("custom ndata test failed");
    }
  }
}

void BaseClarans::clarans_statistics_test()
{
  std::string errm("Problem while testing cluster statistics");
  for (size_t k = 0; k < K; ++k)
  {
    double M      = 0;
    double M_star = 0;
    double R1     = 0;
    double R2     = 0;
    double m      = 0;
    double m_star = 0;

    if (get_ndata(k) > 0)
    {
      for (size_t j = 0; j < get_ndata(k); ++j)
      {
        M = std::max(M, energy_margins[k][j]);
        M_star += (nearest_2_infos[k][j].e_x - get_e1(k, j));
        R1 = std::max(R1, get_d1(k, j));
        R2 = std::max(R2, nearest_2_infos[k][j].d_x);
      }
      m      = M / static_cast<double>(get_ndata(k));
      m_star = M_star / static_cast<double>(get_ndata(k));
    }

    bool test_passed =
      ((M == cluster_statistics[k].M) && (M_star == cluster_statistics[k].M_star) &&
       (R1 == cluster_statistics[k].R1) && (R2 == cluster_statistics[k].R2) &&
       (m == cluster_statistics[k].m) && (m_star == cluster_statistics[k].m_star));
    if (test_passed == false)
    {
      mowri << "\nk = " << k << zentas::Endl;
      mowri << "ndata = " << get_ndata(k) << zentas::Endl;
      mowri << "k_to and k_from : " << k_to << " " << k_from << zentas::Endl;
      mowri << "M \t" << M << "\t" << cluster_statistics[k].M << zentas::Endl;
      mowri << "M_star \t " << M_star << "\t" << cluster_statistics[k].M_star << zentas::Endl;
      mowri << "R1 \t " << R1 << "\t" << cluster_statistics[k].R1 << zentas::Endl;
      mowri << "R2  \t " << R2 << "\t" << cluster_statistics[k].R2 << zentas::Endl;
      mowri << "m  \t " << m << "\t" << cluster_statistics[k].m << zentas::Endl;
      mowri << "m_star \t " << m_star << "\t" << cluster_statistics[k].m_star << zentas::Endl;
      throw zentas::zentas_error(errm);
    }
  }
}

void BaseClarans::custom_cluster_statistics_test() { clarans_statistics_test(); }

void BaseClarans::increment_custom_cluster_statistics(size_t k, size_t j)
{
  cluster_statistics[k].increment(
    get_d1(k, j), get_e1(k, j), nearest_2_infos[k][j].d_x, nearest_2_infos[k][j].e_x);

  // if (nearest_2_infos[k][j].e_x - get_e1(k,j) != energy_margins[k][j]){
  // throw zentas::zentas_error("internal problem detected with energy margin");
  //}
}

void BaseClarans::set_normalised_custom_cluster_statistics(size_t k)
{
  cluster_statistics[k].set_normalised_statistics(get_ndata(k));
}

void BaseClarans::set_to_zero_custom_cluster_statistics(size_t k)
{
  cluster_statistics[k].set_to_zero();
}

void BaseClarans::set_second_nearest(size_t              k_first_nearest,
                                     const double* const distances,
                                     size_t&             k_second_nearest,
                                     double&             d_second_nearest)
{
  d_second_nearest = std::numeric_limits<double>::max();
  for (size_t k = 0; k < k_first_nearest; ++k)
  {
    if (distances[k] < d_second_nearest)
    {
      d_second_nearest = distances[k];
      k_second_nearest = k;
    }
  }

  for (size_t k = k_first_nearest + 1; k < K; ++k)
  {
    if (distances[k] < d_second_nearest)
    {
      d_second_nearest = distances[k];
      k_second_nearest = k;
    }
  }
}

void BaseClarans::refresh_energy_margins(size_t k, size_t j)
{
  energy_margins[k][j] = get_e2(k, j) - get_e1(k, j);
}

void BaseClarans::center_center_info_test_l1(
  const std::vector<XNearestInfo>& center_nearest_center)
{
  for (size_t k = 0; k < K; ++k)
  {
    // size_t nearest = 0;
    double distance = std::numeric_limits<double>::max();
    double adist;
    for (size_t kp = 0; kp < K; ++kp)
    {
      if (kp != k)
      {
        set_center_center_distance_nothreshold(k, kp, adist);
        if (adist < distance)
        {
          distance = adist;
          // nearest = kp;
        }
      }
    }

    if (distance != center_nearest_center[k].d_x)
    {
      std::stringstream ss;
      ss << "Distance center-to-nearest-center not in agreement. The recorded distance is "
         << center_nearest_center[k].d_x << ", and the just computed distance is ";
      ss << distance << ".";
      throw zentas::zentas_error(ss.str());
    }

    if (f_energy(distance) != center_nearest_center[k].e_x)
    {
      throw zentas::zentas_error("energy of nearest center no in agreement");
    }
  }
}

void BaseClarans::acceptance_call_l2(double* const cc,
                                     double* const dists_centers_old_k_to,
                                     double* const d_min_cc,
                                     size_t* const a_min_cc)
{

  // updating cc.
  std::copy(cc + K * k_to, cc + K * (k_to + 1), dists_centers_old_k_to);
  for (size_t k = 0; k < K; ++k)
  {
    set_center_sample_distance_nothreshold(k, k_from, j_from, cc[k * K + k_to]);
    cc[k_to * K + k] = cc[k * K + k_to];
  }
  dists_centers_old_k_to[k_to] = cc[k_to * K + k_to];
  cc[k_to * K + k_to]          = 0.0;  // Hats off to this bug!

  // update a_min_cc and d_min_cc.
  for (size_t k = 0; k < K; ++k)
  {
    if (k == k_to || a_min_cc[k] == k_to)
    {
      d_min_cc[k] = std::numeric_limits<double>::max();
      for (size_t kp = 0; kp < K; ++kp)
      {
        if (kp != k && cc[kp + k * K] < d_min_cc[k])
        {
          d_min_cc[k] = cc[kp + k * K];
          a_min_cc[k] = kp;
        }
      }
    }
    else
    {
      if (cc[k + k_to * K] < d_min_cc[k])
      {
        d_min_cc[k] = cc[k + k_to * K];
        a_min_cc[k] = k_to;
      }
    }
  }
}

void BaseClarans::set_center_center_info_l2(double* const cc,
                                            double* const d_min_cc,
                                            size_t* const a_min_cc)
{

  for (size_t k = 0; k < K; ++k)
  {
    for (size_t kp = k; kp < K; ++kp)
    {
      set_center_center_distance_nothreshold(k, kp, cc[k * K + kp]);
      cc[kp * K + k] = cc[k * K + kp];
    }
  }

  for (size_t k = 0; k < K; ++k)
  {
    d_min_cc[k] = std::numeric_limits<double>::max();
    a_min_cc[k] = 0;
    for (size_t kp = 0; kp < K; ++kp)
    {
      if (k != kp && cc[k * K + kp] < d_min_cc[k])
      {
        d_min_cc[k] = cc[k * K + kp];
        a_min_cc[k] = kp;
      }
    }
  }
}

void BaseClarans::put_nearest_2_infos_margin_in_cluster_final(size_t k_first_nearest,
                                                              size_t k_second_nearest,
                                                              double d_second_nearest,
                                                              double e_second_nearest)
{

  nearest_2_infos[k_first_nearest].emplace_back(
    k_second_nearest, d_second_nearest, e_second_nearest);
  energy_margins[k_first_nearest].push_back(e_second_nearest - get_e1_tail(k_first_nearest));
}

void BaseClarans::put_nearest_2_infos_margin_in_cluster_post_kmeanspp(size_t k1,
                                                                      size_t k2,
                                                                      double d2,
                                                                      double e2)
{
  put_nearest_2_infos_margin_in_cluster_final(k1, k2, d2, e2);
}

void BaseClarans::put_nearest_2_infos_margin_in_cluster(size_t              i,
                                                        size_t              k_first_nearest,
                                                        const double* const distances)
{

  // quelch warning
  i += 0;

  size_t k_second_nearest;
  double d_second_nearest;
  set_second_nearest(k_first_nearest, distances, k_second_nearest, d_second_nearest);
  double e_second_nearest = f_energy(d_second_nearest);

  put_nearest_2_infos_margin_in_cluster_final(
    k_first_nearest, k_second_nearest, d_second_nearest, e_second_nearest);
}

void BaseClarans::reset_sample_nearest_2_infos_margin(size_t              k,
                                                      size_t              j,
                                                      size_t              nearest_center,
                                                      const double* const distances)
{
  size_t k_second_nearest;
  double d_second_nearest;
  set_second_nearest(nearest_center, distances, k_second_nearest, d_second_nearest);
  double e_second_nearest = f_energy(d_second_nearest);

  reset_second_nearest_info(k, j, k_second_nearest, d_second_nearest, e_second_nearest);

  energy_margins[k][j] = e_second_nearest - get_e1(k, j);
}

void BaseClarans::nearest_2_infos_margin_append(size_t k_new, size_t k, size_t j)
{
  nearest_2_infos[k_new].push_back(nearest_2_infos[k][j]);
  energy_margins[k_new].push_back(energy_margins[k][j]);
}

void BaseClarans::nearest_2_infos_margin_replace_with_last(size_t k, size_t j)
{
  nearest_2_infos[k][j] = *(nearest_2_infos[k].end() - 1);
  energy_margins[k][j]  = energy_margins[k].back();
}

void BaseClarans::nearest_2_infos_margin_replace_with(size_t k1, size_t j1, size_t k2, size_t j2)
{
  nearest_2_infos[k1][j1] = nearest_2_infos[k2][j2];
  energy_margins[k1][j1]  = energy_margins[k2][j2];
}

void BaseClarans::nearest_2_infos_margin_remove_last(size_t k)
{
  nearest_2_infos[k].pop_back();
  energy_margins[k].pop_back();
}

void BaseClarans::nearest_2_infos_margin_test()
{
  for (size_t k = 0; k < K; ++k)
  {
    for (size_t j = 0; j < get_ndata(k); ++j)
    {

      std::vector<double> distances(K);

      double d_second_nearest = std::numeric_limits<double>::max();
      size_t k_second_nearest;

      for (size_t k2 = 0; k2 < K; ++k2)
      {
        set_center_sample_distance_nothreshold(k2, k, j, distances[k2]);

        if (k2 != get_a1(k, j))
        {
          if (distances[k2] < d_second_nearest)
          {
            d_second_nearest = distances[k2];
            k_second_nearest = k2;
          }
        }
      }

      double e_second_nearest = f_energy(d_second_nearest);

      // if (k_second_nearest != nearest_2_infos[k][j].a_x){
      // throw zentas::zentas_error("k_second_nearest != k_second_nearest");
      //}

      if (d_second_nearest != nearest_2_infos[k][j].d_x)
      {

        std::stringstream errm;

        errm << "error detected : d_second_nearest != d_second_nearest\n";
        errm << "k=" << k << "\n";
        errm << "j=" << j << "\n";
        errm << "the " << j << "'th sample in cluster " << k << " is " << string_for_sample(k, j)
             << "\n";
        errm << "cluster size is " << nearest_2_infos[k].size() << "\n";
        errm << std::setprecision(20);
        errm << "get_a1(k,j)=" << get_a1(k, j) << "\n";
        errm << "just computed second nearest center index: " << k_second_nearest << "\n";
        errm << "the " << k_second_nearest
             << "'th center is: " << string_for_center(k_second_nearest) << "\n";
        errm << "just computed distance to this center is: " << d_second_nearest << "\n";
        errm << "the recorded second nearest center index: " << nearest_2_infos[k][j].a_x << "\n";
        errm << "the " << nearest_2_infos[k][j].a_x << "'th center is "
             << string_for_center(nearest_2_infos[k][j].a_x) << "\n";
        errm << "the recorded distance to this center is " << nearest_2_infos[k][j].d_x << "\n";

        throw zentas::zentas_error(errm.str());
      }

      if (e_second_nearest != nearest_2_infos[k][j].e_x)
      {
        throw zentas::zentas_error("e_second_nearest != e_second_nearest");
      }

      double m_v = nearest_2_infos[k][j].e_x - get_e1(k, j);
      if (m_v != energy_margins[k][j])
      {
        mowri << "\nk : " << k << " \t j : " << j << zentas::Endl;
        mowri << "\nm_v : " << m_v << " \t energy_margins[k][j] : " << energy_margins[k][j]
              << zentas::Endl;
        throw zentas::zentas_error("m_v != energy_margin");
      }
    }
  }
}

void BaseClarans::basic_clarans_update_sample_info()
{

  double adistance;

  // cluster k_to : full reset.
  for (size_t j = 0; j < get_ndata(k_to); ++j)
  {
    reset_sample_infos(k_to, j);
  }

  for (size_t k = 0; k < K; ++k)
  {
    if (k != k_to)
    {
      for (size_t j = 0; j < get_ndata(k); ++j)
      {
        if (get_a2(k, j) == k_to)
        {
          reset_sample_infos(k, j);
        }
        else
        {
          set_center_sample_distance(k_to, k, j, get_d2(k, j), adistance);
          if (adistance < get_d1(k, j))
          {
            reset_second_nearest_info(k, j, get_a1(k, j), get_d1(k, j), get_e1(k, j));
            reset_nearest_info(k, j, k_to, adistance, f_energy(adistance));

            energy_margins[k][j] = get_e2(k, j) - get_e1(k, j);
          }
          else if (adistance < get_d2(k, j))
          {
            reset_second_nearest_info(k, j, k_to, adistance, f_energy(adistance));
            energy_margins[k][j] = get_e2(k, j) - get_e1(k, j);
          }
        }
      }
    }
  }
}

double BaseClarans::get_delta_E_l0(size_t k1, size_t k2, size_t j2)
{

  double dist_to_proposed;
  double adist;

  double delta_E = 0;

  // members of k1
  for (size_t j = 0; j < get_ndata(k1); ++j)
  {
    set_sample_sample_distance(k1, j, k2, j2, get_d2(k1, j), dist_to_proposed);

    if (dist_to_proposed < get_d2(k1, j))
    {
      delta_E += (f_energy(dist_to_proposed) - get_e1(k1, j));
    }
    else
    {
      delta_E += energy_margins[k1][j];
    }
  }

  // center of k1
  double k1_post_dist = get_center_sample_distance_nothreshold(k1, k2, j2);
  for (size_t k = 0; k < k1; ++k)
  {
    set_center_center_distance_nothreshold(k, k1, adist);
    k1_post_dist = std::min(k1_post_dist, adist);
  }
  for (size_t k = k1 + 1; k < K; ++k)
  {
    set_center_center_distance_nothreshold(k, k1, adist);
    k1_post_dist = std::min(k1_post_dist, adist);
  }
  delta_E += f_energy(k1_post_dist);

  // all other members
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k1)
    {
      for (size_t j = 0; j < get_ndata(k); ++j)
      {
        set_sample_sample_distance(k, j, k2, j2, get_d1(k, j), dist_to_proposed);
        if (dist_to_proposed < get_d1(k, j))
        {
          delta_E += (f_energy(dist_to_proposed) - get_e1(k, j));
        }
      }
    }
  }

  return delta_E;
}

void BaseClarans::update_sample_info_l1(const double* const dists_centers_old_k_to,
                                        const double* const dists_centers_new_k_to)
{

  std::unique_ptr<double[]> up_dists_centers_min_pr(new double[K]);
  auto                      dists_centers_min_pr = up_dists_centers_min_pr.get();
  for (size_t k = 0; k < K; ++k)
  {
    /* the decrease here is necessary to prevent discrepency caused to non-commutative float
     * addition, marked by TEST74.
     * the problem arises with l1, li metrices, as well as l2 in 1 dimension.  */
    dists_centers_min_pr[k] =
      std::min(dists_centers_old_k_to[k], dists_centers_new_k_to[k]) * (1. - 1e-6);
  }

  std::vector<size_t> k_non_eliminated;
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k_to)
    {
      /* a TEST74 (grep) */
      if (dists_centers_min_pr[k] <= cluster_statistics[k].R1 + cluster_statistics[k].R2)
      {
        k_non_eliminated.push_back(k);
      }
    }
  }

  std::vector<std::thread> threads;
  // add nthreads threads, each with 1 / nthreads of the samples to process
  for (size_t ti = 0; ti < get_nthreads(); ++ti)
  {
    threads.emplace_back([this, ti, &k_non_eliminated, dists_centers_min_pr]() {
      // cluster k_to : full reset,
      reset_multiple_sample_infos(k_to,
                                  get_start(ti, get_nthreads(), 0, get_ndata(k_to)),
                                  get_end(ti, get_nthreads(), 0, get_ndata(k_to)));
      // all other non-eliminated clusters,
      for (auto& k : k_non_eliminated)
      {
        pll_update_sample_info_l1(k,
                                  get_start(ti, get_nthreads(), 0, get_ndata(k)),
                                  get_end(ti, get_nthreads(), 0, get_ndata(k)),
                                  dists_centers_min_pr[k]);
      }
    });
  }

  for (auto& t : threads)
  {
    t.join();
  }
}

void BaseClarans::update_sample_info_l23(const double* const dists_centers_old_k_to,
                                         const double* const cc)
{

  const double* const dists_centers_new_k_to = cc + k_to * K;

  std::unique_ptr<double[]> up_dists_centers_min_pr(new double[K]);
  auto                      dists_centers_min_pr = up_dists_centers_min_pr.get();
  for (size_t k = 0; k < K; ++k)
  {
    /* a TEST74 correction (grep) */
    dists_centers_min_pr[k] =
      std::min(dists_centers_old_k_to[k], dists_centers_new_k_to[k]) * (1. - 1e-6);
  }

  std::vector<size_t> non_eliminated;
  for (size_t k = 0; k < K; ++k)
  {
    if (k != k_to)
    {
      if (dists_centers_min_pr[k] <= cluster_statistics[k].R1 + cluster_statistics[k].R2)
      {
        non_eliminated.push_back(k);
      }
    }
  }

  std::vector<std::thread> threads;

  for (size_t ti = 0; ti < get_nthreads(); ++ti)
  {
    threads.emplace_back(
      [this, ti, &non_eliminated, dists_centers_old_k_to, cc, dists_centers_min_pr]() {
        // cluster k_to : full reset,
        update_k_to_sample_info_l2(get_start(ti, get_nthreads(), 0, get_ndata(k_to)),
                                   get_end(ti, get_nthreads(), 0, get_ndata(k_to)),
                                   dists_centers_old_k_to,
                                   cc);
        // all other non-eliminated clusters,
        for (auto& k : non_eliminated)
        {
          pll_update_sample_info_l23(k,
                                     get_start(ti, get_nthreads(), 0, get_ndata(k)),
                                     get_end(ti, get_nthreads(), 0, get_ndata(k)),
                                     dists_centers_min_pr,
                                     cc);
        }
      });
  }

  for (auto& t : threads)
  {
    t.join();
  }
}

double
BaseClarans::get_delta_E_l1(size_t k1, size_t k2, size_t j2, double d_nearest_k1, bool serial)
{
  double delta_E = 0;

  std::unique_ptr<double[]> up_dists_centers_j2(new double[K]);
  auto                      dists_centers_j2 = up_dists_centers_j2.get();
  /* if this were l(>1) I would consider parallelising it */
  for (size_t k = 0; k < K; ++k)
  {
    set_center_sample_distance_nothreshold(k, k2, j2, dists_centers_j2[k]);
  }

  // center of k1
  delta_E += f_energy(std::min(d_nearest_k1, dists_centers_j2[k1]));

  // members of k1
  delta_E += get_delta_E_k1_l12(k1, k2, j2, dists_centers_j2[k1], serial);

  // all other members
  delta_E += get_delta_E_not_k1_l1(k1, k2, j2, dists_centers_j2, serial);

  return delta_E;
}

//    bool evaluate_proposal_l3(size_t k1, size_t k2, size_t j2, double d_nearest_k1, const double
//    * const cc);

double BaseClarans::get_delta_E_l2(
  size_t k1, size_t k2, size_t j2, double d_nearest_k1, const double* const cc, bool serial)
{

  double delta_E = 0;
  double dist_k1_j2;
  set_center_sample_distance(k1, k2, j2, cc[k1 + K * k2] + get_d1(k2, j2), dist_k1_j2);

  // center of k1
  delta_E += f_energy(std::min(d_nearest_k1, dist_k1_j2));

  // members of k1
  delta_E += get_delta_E_k1_l12(k1, k2, j2, dist_k1_j2, serial);

  // all other members
  delta_E += get_delta_E_not_k1_l2(k1, k2, j2, cc, serial);

  return delta_E;
}

void BaseClarans::update_center_center_info_l1(std::vector<XNearestInfo>& center_nearest_center,
                                               double* const              dists_centers_old_k_to,
                                               double* const              dists_centers_new_k_to)
{

  // quelch warning
  (void)dists_centers_old_k_to;

  std::unique_ptr<double[]> up_distances(new double[K]);
  auto                      distances = up_distances.get();

  /* clean update for k_to */

  reset_1_nearest_excluding(center_nearest_center[k_to], k_to, dists_centers_new_k_to);
  // set_center_nearest_center(k_to, dists_centers_new_k_to);

  for (size_t k = 0; k < K; ++k)
  {
    if (k != k_to)
    {
      if (center_nearest_center[k].a_x == k_to)
      {
        /* still k_to, as k_to is now even closer than before the move */
        if (dists_centers_new_k_to[k] < center_nearest_center[k].d_x)
        {
          center_nearest_center[k].reset(
            k_to, dists_centers_new_k_to[k], f_energy(dists_centers_new_k_to[k]));
          // reset_center_nearest_center(k, k_to, dists_centers_new_k_to[k],
          // f_energy(dists_centers_new_k_to[k]));
        }
        else
        {
          set_center_center_distances(k, distances);

          // set_center_nearest_center(k, distances);
          reset_1_nearest_excluding(center_nearest_center[k], k, distances);
        }
      }
      else
      {
        if (dists_centers_new_k_to[k] < center_nearest_center[k].d_x)
        {
          center_nearest_center[k].reset(
            k_to, dists_centers_new_k_to[k], f_energy(dists_centers_new_k_to[k]));
          // reset_center_nearest_center(k, k_to, dists_centers_new_k_to[k],
          // f_energy(dists_centers_new_k_to[k]));
        }
      }
    }
  }
}

void BaseClarans::set_center_center_info_l1(std::vector<XNearestInfo>& center_nearest_center)
{

  /* I could create an empty constructor for XNearestInfo,
   * but I prefer not to, to make sure things are initialised correctly,
   * hence this unelegant code: */
  if (center_nearest_center.size() != K)
  {
    throw zentas::zentas_error("center_nearest_center should be of size K (jn 20/7/2017)");
  }

  std::unique_ptr<double[]> up_distances(new double[K]);
  auto                      distances = up_distances.get();
  for (size_t k = 0; k < K; ++k)
  {
    set_center_center_distances(k, distances);

    reset_1_nearest_excluding(center_nearest_center[k], k, distances);
  }
}

// TODO : cc in level 2,3 should be freed.
void BaseClarans::base_clarans_custom_initialise_refinement()
{
  prd->initialise_from_n1n2(nearest_1_infos, nearest_2_infos);
}

void BaseClarans::custom_initialise_refinement() { base_clarans_custom_initialise_refinement(); }

void BaseClarans::custom_rf_clear_initmem()
{
  energy_margins.resize(0);
  nearest_2_infos.resize(0);
  cluster_statistics.resize(0);
  // TODO level 3 clear cc
}
}
