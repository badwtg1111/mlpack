#include "thor/thor.h"
#include "fastlib/fastlib.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*

As two-variable functions:

\rho(i, k) = \sum_{j != i, j != k} max(0, S(j,k) - \alpha(j,k))

\alpha(i, k) = min(0, \max_{j != k} S(j,j) + \alpha(j,j) + \rho(i,j))

As one-variable rho:

\rho(k) = \sum_{j != k} max(0, S(j,k) - \alpha(j,k))

\sum max(0, S(j,k) - \alpha(j,k)) - max(0, S(k,k) - alpha(k,k))

\alpha(i, k) = min(0, \max_{j != k} S(j,j) + \alpha(j,j)
     + \rho(i) - max(0, S(i, j) - \alpha(i, j)))

\alpha(i) = min(0, \max^2{j}
      S(j,j) + \alpha(j,j) + \rho(j) - max(0, S(i, j) - \alpha(i, j)))
 except when i = j in which case we don't need to do the second part

      S(j,j) + \alpha(j,j) + \rho(j) - S(i, j) + min(S(i,j), \alpha(i, j))
*/

#define SIMILARITY_MAX 0

struct AffinityCommon {
  struct Param {
   public:
    /** Self-pereference. */
    double pref;
    /** The damping factor. */
    double lambda;
    /** The proportion of points to prime as exemplars. */
    double prime;
    /** The dimensionality. */
    index_t dim;

    OT_DEF(Param) {
      OT_MY_OBJECT(pref);
      OT_MY_OBJECT(lambda);
      OT_MY_OBJECT(prime);
      OT_MY_OBJECT(dim);
    }

   public:
    void Init(datanode *module) {
      pref = fx_param_double_req(module, "pref");
      lambda = fx_param_double(module, "lambda", 0.6);
      prime = fx_param_double(module, "prime", 0.002);
    }
  };

  /** The bounding type. Required by THOR. */
  typedef DHrectBound<2> Bound;

//  struct MomentInfo {
//   public:
//    Vector mass;
//    double sumsq;
//    index_t count;
//
//    OT_DEF_BASIC(MomentInfo) {
//      OT_MY_OBJECT(mass);
//      OT_MY_OBJECT(sumsq);
//      OT_MY_OBJECT(count);
//    }
//
//   public:
//    void Init(int dim) {
//      mass.Init(dim);
//      Reset();
//    }
//
//    void Reset() {
//      mass.SetZero();
//      sumsq = 0;
//      count = 0;
//    }
//
//    void Add(index_t count_in, const Vector& mass_in, double sumsq_in) {
//      if (unlikely(count_in != 0)) {
//        la::AddTo(mass_in, &mass);
//        sumsq += sumsq_in;
//        count += count_in;
//      }
//    }
//
//    void Add(const MomentInfo& other) {
//      Add(other.count, other.mass, other.sumsq);
//    }
//
//    void Add(const Vector& v) {
//      Add(1, v, la::Dot(v, v));
//    }
//
//    double ComputeDistanceSqSum(const Vector& q) const {
//      double quadratic_term =
//          + count * la::Dot(q, q)
//          - 2.0 * la::Dot(q, mass)
//          + sumsq;
//      return quadratic_term;
//    }
//
//    double ComputeDistanceSqSum(double distance_squared,
//        double center_dot_center) const {
//      //q*q - 2qr + rsumsq
//      //q*q - 2qr + r*r - r*r
//      double quadratic_term =
//          (distance_squared - center_dot_center) * count
//          + sumsq;
//
//      return quadratic_term;
//    }
//
//    DRange ComputeDistanceSqSumRange(
//        const Bound& query_bound) const {
//      DRange distsq_sum_bound;
//      Vector center;
//      double inv_count = 1.0 / count;
//      double center_dot_center = la::Dot(mass, mass) * inv_count * inv_count;
//
//      DEBUG_ASSERT(count != 0);
//
//      center.Copy(mass);
//      for (index_t i = center.length(); i--;) {
//        center[i] *= inv_count;
//      }
//
//      distsq_sum_bound.lo = ComputeDistanceSqSum(
//          query_bound.MaxDistanceSq(center), center_dot_center);
//      distsq_sum_bound.hi = ComputeDistanceSqSum(
//          query_bound.MinDistanceSq(center), center_dot_center);
//
//      return distsq_sum_bound;
//    }
//
//    bool is_empty() const {
//      return likely(count == 0);
//    }
//  };

  /**
   * Alpha corresponds to "maximum availability" with the != k condition.
   */
  struct Alpha {
   public:
    double max2;
    double max1_sim;
    double max1;
    index_t max1_index;

    OT_DEF(Alpha) {
      OT_MY_OBJECT(max2);
      OT_MY_OBJECT(max1_sim);
      OT_MY_OBJECT(max1);
      OT_MY_OBJECT(max1_index);
    }

   public:
    double get(index_t i) const {
      if (unlikely(i == max1_index)) {
        return max2;
      } else {
        return max1;
      }
    }
  };

  struct CombinedInfo {
    /** Maximum availability of the point. */
    Alpha alpha;
    /** Sum of responsibilities. */
    double rho;

    OT_DEF(CombinedInfo) {
      OT_MY_OBJECT(alpha);
      OT_MY_OBJECT(rho);
    }
  };

  class Point {
   private:
    Vector vec_;
    CombinedInfo info_;

    OT_DEF(Point) {
      OT_MY_OBJECT(vec_);
      OT_MY_OBJECT(info_);
    }

   public:
    void Init(const Param& param, const DatasetInfo& schema) {
      vec_.Init(schema.n_features());
      info_.alpha.max1 = 0;
      info_.alpha.max2 = 0;
      info_.alpha.max1_index = -1;
      info_.alpha.max1_sim = 0;
      info_.rho = param.pref;
    }

    void Set(const Param& param, const Vector& data) {
      vec_.CopyValues(data);
      // Randomly prime points to be exemplars.
      if (math::Random(0.0, 1.0) < param.prime) {
        info_.rho = -param.pref / 2;
      }
    }

    const Vector& vec() const { return vec_; }
    Vector& vec() { return vec_; }
    
    const CombinedInfo& info() const { return info_; }
    CombinedInfo& info() { return info_; }
  };

  struct CombinedStat {
   public:
    DRange alpha;
    DRange rho;

    OT_DEF(CombinedStat) {
      OT_MY_OBJECT(alpha);
      OT_MY_OBJECT(rho);
    }

   public:
    void Init(const Param& param) {
      Reset(param);
    }
    void Reset(const Param& param) {
      alpha.InitEmptySet();
      rho.InitEmptySet();
    }
    void Accumulate(const Param& param, const Point& point) {
      alpha |= DRange(point.info().alpha.max2, point.info().alpha.max1);
      rho |= point.info().rho;
    }
    void Accumulate(const Param& param,
        const CombinedStat& stat, const Bound& bound, index_t n) {
      alpha |= stat.alpha;
      rho |= stat.rho;
    }
    void Postprocess(const Param& param, const Bound& bound, index_t n) {}
  };

  typedef ThorNode<Bound, CombinedStat> Node;

  struct Helpers {
    static double Similarity(double distsq) {
      return -distsq;
    }
    static double Similarity(const Vector& a, const Vector& b) {
      //uint32 anum = (mem::PointerAbsoluteAddress(a.ptr()) * 315187727);
      //uint32 bnum = (mem::PointerAbsoluteAddress(b.ptr()) * 210787727);
      //uint32 val = ((anum - bnum) >> 16) & 0xfff;
      //double noise = (1.0e-5 / 4096) * val;
      return Similarity(la::DistanceSqEuclidean(a, b));
    }
    static double Similarity(
        const Param& param,
        const Vector& q, index_t q_index,
        const Vector& r, index_t r_index) {
      if (unlikely(q_index == r_index)) {
        return param.pref;
      } else {
        return Similarity(q, r);
      }
    }
    static double SimilarityHi(
        const Param& param,
        const Node& a, const Node& b) {
      double distsq = a.bound().MinDistanceSq(b.bound());
      double hi = Similarity(distsq);
      if (a.begin() < b.end() && b.begin() < a.end()
          && param.pref > hi) {
        hi = param.pref;
      }
      return hi;
    }
    static double SimilarityLo(
        const Param& param,
        const Node& a, const Node& b) {
      double distsq = a.bound().MaxDistanceSq(b.bound());
      double lo = Similarity(distsq);
      if (a.begin() < b.end() && b.begin() < a.end()
          && param.pref < lo) {
        lo = param.pref;
      }
      return lo;
    }
    static double SimilarityHi(
        const Param& param,
        const Vector& a, index_t a_index, const Node& b) {
      double distsq = b.bound().MinDistanceSq(a);
      double hi = Similarity(distsq);
      if (a_index < b.end() && a_index >= b.begin()
          && param.pref > hi) {
        hi = param.pref;
      }
      return hi;
    }
    static double SimilarityLo(
        const Param& param,
        const Vector& a, index_t a_index, const Node& b) {
      double distsq = b.bound().MaxDistanceSq(a);
      double lo = Similarity(distsq);
      if (a_index < b.end() && a_index >= b.begin()
          && param.pref < lo) {
        lo = param.pref;
      }
      return lo;
    }
  };
};

class AffinityAlpha {
 public:
  typedef AffinityCommon::Point QPoint;
  typedef AffinityCommon::Point RPoint;

  typedef AffinityCommon::Alpha Alpha;

  typedef AffinityCommon::Param Param;

  typedef AffinityCommon::Node QNode;
  typedef AffinityCommon::Node RNode;

  typedef BlankGlobalResult GlobalResult;

  typedef BlankQPostponed QPostponed;

  struct Delta {
   public:
    DRange alpha;

    OT_DEF(Delta) {
      OT_MY_OBJECT(alpha);
    }

   public:
    void Init(const Param& param) {
    }
  };

  struct QResult {
   public:
    Alpha alpha;

    OT_DEF(QResult) {
      OT_MY_OBJECT(alpha);
    }

   public:
    void Init(const Param& param) {
      Reset();
    }
    void Reset() {
      alpha.max1 = -DBL_MAX;
      alpha.max2 = -DBL_MAX;
      alpha.max1_index = -1;
      alpha.max1_sim = -DBL_MAX;
    }
    void Postprocess(const Param& param,
        const QPoint& q, index_t q_index, const RNode& r_root) {}
    void ApplyPostponed(const Param& param,
        const QPostponed& postponed, const QPoint& q, index_t q_i) {}
  };

  struct QSummaryResult {
   public:
    DRange alpha;

    OT_DEF(QSummaryResult) {
      OT_MY_OBJECT(alpha);
    }

   public:
    void Init(const Param& param) {
      alpha.Init(-DBL_MAX, -DBL_MAX);
    }
    void ApplySummaryResult(const Param& param, const QSummaryResult& summary_result) {
      alpha.MaxWith(summary_result.alpha);
    }
    void ApplyDelta(const Param& param, const Delta& delta) {
      alpha.MaxWith(delta.alpha);
    }
    void ApplyPostponed(const Param& param,
        const QPostponed& postponed, const QNode& q_node) {}

    void StartReaccumulate(const Param& param, const QNode& q_node) {
      alpha.InitEmptySet();
    }
    void Accumulate(const Param& param, const QResult& result) {
      alpha |= DRange(result.alpha.max2, result.alpha.max1);
    }
    void Accumulate(const Param& param,
        const QSummaryResult& result, index_t n_points) {
      alpha |= result.alpha;
    }
    void FinishReaccumulate(const Param& param, const QNode& q_node) {}
  };

  struct PairVisitor {
   public:
    Alpha old_alpha;
    Alpha alpha;

   public:
    void Init(const Param& param) {}

    bool StartVisitingQueryPoint(const Param& param,
        const QPoint& q, index_t q_index,
        const RNode& r_node, const QSummaryResult& unapplied_summary_results,
        QResult* q_result, GlobalResult* global_result) {
      double alpha_hi;

      alpha = q_result->alpha;
      old_alpha = q.info().alpha;

      if (unlikely(q_index >= r_node.begin() && q_index < r_node.end())) {
        alpha_hi = r_node.stat().rho.hi + old_alpha.max1;
      } else {
        double sim = AffinityCommon::Helpers::Similarity(
            r_node.bound().MinDistanceSq(q.vec()));
        alpha_hi = min(
            min(sim, old_alpha.max1) + r_node.stat().rho.hi,
            sim);
      }

      return (alpha_hi > alpha.max2);
    }
    void VisitPair(const Param& param,
        const QPoint& q, index_t q_index, const RPoint& r, index_t r_index) {
      double candidate_alpha;
      double sim = AffinityCommon::Helpers::Similarity(
          la::DistanceSqEuclidean(q.vec(), r.vec()));

      if (likely(q_index != r_index)) {
        candidate_alpha = min(
            min(old_alpha.get(r_index), sim) + r.info().rho,
            sim);
      } else {
        candidate_alpha = r.info().rho + old_alpha.get(r_index);
      }
      if (unlikely(candidate_alpha > alpha.max2)) {
        if (unlikely(candidate_alpha > alpha.max1)) {
          alpha.max2 = alpha.max1;
          alpha.max1 = candidate_alpha;
          alpha.max1_index = r_index;
          alpha.max1_sim = sim;
        } else {
          alpha.max2 = candidate_alpha;
        }
      }
    }
    void FinishVisitingQueryPoint(const Param& param,
        const QPoint& q, index_t q_index,
        const RNode& r_node, const QSummaryResult& unapplied_summary_results,
        QResult* q_result, GlobalResult* global_result) {
      q_result->alpha = alpha;
    }
  };

  class Algorithm {
   public:
    static bool ConsiderPairIntrinsic(const Param& param,
        const QNode& q_node, const RNode& r_node,
        Delta* delta,
        GlobalResult* global_result, QPostponed* q_postponed) {
      double sim_lo = AffinityCommon::Helpers::SimilarityLo(
          param, q_node, r_node);
      delta->alpha.lo = min(min(q_node.stat().alpha.lo, sim_lo)
          + r_node.stat().rho.lo, sim_lo);

      if (q_node.begin() < r_node.end() && r_node.begin() < q_node.end()) {
        delta->alpha.hi =
            q_node.stat().alpha.hi + r_node.stat().rho.hi;
      } else {
        double sim_hi = AffinityCommon::Helpers::Similarity(
            q_node.bound().MinDistanceSq(r_node.bound()));
        delta->alpha.hi = min(min(q_node.stat().alpha.hi, sim_hi)
            + r_node.stat().rho.hi, sim_hi);
      }

      return true;
    }
    static bool ConsiderPairExtrinsic(const Param& param,
        const QNode& q_node, const RNode& r_node, const Delta& delta,
        const QSummaryResult& q_summary_result, const GlobalResult& global_result,
        QPostponed* q_postponed) {
      return (delta.alpha.hi >= q_summary_result.alpha.lo);
    }
    static bool ConsiderQueryTermination(const Param& param,
        const QNode& q_node,
        const QSummaryResult& q_summary_result, const GlobalResult& global_result,
        QPostponed* q_postponed) {
      return true;
    }
    static double Heuristic(const Param& param,
        const QNode& q_node, const RNode& r_node, const Delta& delta) {
      //return -delta.alpha.hi;
      return r_node.bound().MinToMidSq(q_node.bound());
    }
  };
};

class AffinityRho {
 public:
  typedef AffinityCommon::Alpha Alpha;

  typedef AffinityCommon::Point QPoint;
  typedef AffinityCommon::Point RPoint;

  typedef AffinityCommon::Param Param;

  typedef AffinityCommon::Node QNode;
  typedef AffinityCommon::Node RNode;

  typedef BlankGlobalResult GlobalResult;

  typedef BlankQPostponed QPostponed;

  struct Delta {
   public:
    DRange d_rho;

    OT_DEF(Delta) {
      OT_MY_OBJECT(d_rho);
    }

   public:
    void Init(const Param& param) {
    }
  };

  struct QResult {
   public:
    double rho;

    OT_DEF(QResult) {
      OT_MY_OBJECT(rho);
    }

   public:
    void Init(const Param& param) {
      Reset();
    }
    void Reset() {
      rho = 0;
    }
    void Postprocess(const Param& param,
        const QPoint& q, index_t q_index, const RNode& r_root) {
      // Subtract out the improperly computed rho and substitute a new one
      double responsibility =
         AffinityCommon::Helpers::Similarity(q.vec(), q.vec())
         - q.info().alpha.get(q_index);
      rho -= (responsibility + fabs(responsibility)) / 2;
      double self_responsibility = param.pref - q.info().alpha.get(q_index);
      rho += self_responsibility;
    }
    void ApplyPostponed(const Param& param,
        const QPostponed& postponed, const QPoint& q, index_t q_index) {
    }
  };

  typedef BlankQSummaryResult QSummaryResult;

  struct PairVisitor {
   public:
    double prho;
    double arho;

   public:
    void Init(const Param& param) {}
    bool StartVisitingQueryPoint(const Param& param,
        const QPoint& q, index_t q_index,
        const RNode& r_node, const QSummaryResult& unapplied_summary_results,
        QResult* q_result, GlobalResult* global_result) {
      // do the point-node prune check
      double sim_hi = AffinityCommon::Helpers::SimilarityHi(
          param, q.vec(), q_index, r_node);
      if (sim_hi < r_node.stat().alpha.lo) {
         return false;
      } else {
        prho = arho = 0;
        return true;
      }
    }
    void VisitPair(const Param& param,
        const QPoint& q, index_t q_index,
        const RPoint& r, index_t r_index) {
      double responsibility =
          AffinityCommon::Helpers::Similarity(q.vec(), r.vec())
          - r.info().alpha.get(q_index);
      prho += responsibility;
      arho += fabs(responsibility);
    }
    void FinishVisitingQueryPoint(const Param& param,
        const QPoint& q, index_t q_index,
        const RNode& r_node, const QSummaryResult& unapplied_summary_results,
        QResult* q_result, GlobalResult* global_result) {
      prho = (prho + arho) / 2;
      q_result->rho += prho;
    }
  };

  class Algorithm {
   public:
    static bool ConsiderPairIntrinsic(const Param& param,
        const QNode& q_node, const RNode& r_node,
        Delta* delta,
        GlobalResult* global_result, QPostponed* q_postponed) {
      double sim_hi = AffinityCommon::Helpers::SimilarityHi(
          param, q_node, r_node);
      double sim_lo = AffinityCommon::Helpers::SimilarityLo(
          param, q_node, r_node);

      delta->d_rho.lo = math::ClampNonNegative(sim_lo - r_node.stat().alpha.hi)
          * r_node.count();
      delta->d_rho.hi = math::ClampNonNegative(sim_hi - r_node.stat().alpha.lo)
          * r_node.count();

      return delta->d_rho.hi != 0;
    }
    static bool ConsiderPairExtrinsic(const Param& param,
        const QNode& q_node, const RNode& r_node, const Delta& delta,
        const QSummaryResult& q_summary_result, const GlobalResult& global_result,
        QPostponed* q_postponed) {
      return true;
    }
    static bool ConsiderQueryTermination(const Param& param,
        const QNode& q_node,
        const QSummaryResult& q_summary_result, const GlobalResult& global_result,
        QPostponed* q_postponed) {
      return true;
    }
    static double Heuristic(const Param& param,
        const QNode& q_node, const RNode& r_node, const Delta& delta) {
      // TODO: If approximating, favor upper bound
      return 0;
    }
  };
};

struct Cluster {
  Vector exemplar;
  Vector centroid;
  index_t count;
};

template<typename Visitor>
struct VisitorReductor {
  void Reduce(const Visitor& right, Visitor *left) const {
    left->Accumulate(right);
  }
};

struct ApplyAlphas {
 public:
  const AffinityCommon::Param *param;
  double sum_alpha1;
  double sum_alpha2;
  double netsim;

  OT_DEF(ApplyAlphas) {
    OT_MY_OBJECT(sum_alpha1);
    OT_MY_OBJECT(sum_alpha2);
    OT_MY_OBJECT(netsim);
  }

  OT_FIX(ApplyAlphas) {
    // the pointer shall not be sent over the internet
    param = NULL;
  }

 public:
  void Init(const AffinityCommon::Param *param_in) {
    param = param_in;
    sum_alpha1 = 0;
    sum_alpha2 = 0;
    netsim = 0;
  }

  void Update(index_t index,
      AffinityCommon::Point *point, AffinityAlpha::QResult* result) {
    point->info().alpha = result->alpha;
    sum_alpha1 += result->alpha.max1;
    sum_alpha2 += result->alpha.max2;
    if (point->info().rho > 0) {
      netsim += param->pref;
    } else {
      netsim += result->alpha.max1_sim;
    }
  }

  void Accumulate(const ApplyAlphas& other) {
    sum_alpha1 += other.sum_alpha1;
    sum_alpha2 += other.sum_alpha2;
    netsim += other.netsim;
  }
};

inline double damp(double lambda, double prev, double next) {
  return (prev - next) * lambda + next;
}

struct ApplyRhos {
 public:
  const AffinityCommon::Param *param;
  index_t n_changed;
  index_t hash;
  index_t n_exemplars;
  double squared_changed;
  double squared_difference;
  double sum;

  OT_DEF(ApplyRhos) {
    OT_MY_OBJECT(n_changed);
    OT_MY_OBJECT(hash);
    OT_MY_OBJECT(n_exemplars);
    OT_MY_OBJECT(squared_difference);
    OT_MY_OBJECT(squared_changed);
    OT_MY_OBJECT(sum);
  }

  OT_FIX(ApplyRhos) {
    // the pointer shall not be sent over the internet
    param = NULL;
  }

 public:
  void Init(const AffinityCommon::Param *param_in) {
    param = param_in;
    hash = 0;
    n_changed = 0;
    n_exemplars = 0;
    squared_difference = 0;
    squared_changed = 0;
    sum = 0;
  }

  void Update(index_t index,
      AffinityCommon::Point *point, AffinityRho::QResult* result) {
    double old_rho = point->info().rho;
    double new_rho = result->rho;
    bool was_exemplar = (old_rho > 0);
    bool wants_exemplar = (result->rho > 0);

    if (was_exemplar != wants_exemplar) {
      squared_changed += math::Sqr(new_rho - old_rho);
      // Random damping so far has gotten to most consistent convergence.
      new_rho = damp(math::Random(0.0, 1.0), old_rho, new_rho);
      n_changed++;
      hash ^= index;
    }

    new_rho = damp(param->lambda, old_rho, new_rho);

    //double idampfact =
    //    pow(fabs(old_rho) / (fabs(old_rho - new_rho) + fabs(old_rho)), param->dampness);
    //new_rho = damp((1 - idampfact), old_rho, new_rho);
    // First, damp according to the specified damping factor.
    //new_rho = damp(param->lambda, old_rho, new_rho);
    // Next, damp according to a variable damping factor that penalizes
    // large changes.
    //double dampfact = atan2(param->dampness * fabs(new_rho - old_rho),
    //    fabs(old_rho)) * (2.0 / math::PI);

// NOTE: The bottom two give better netsims on some problems but they're
// quite bad for convergence.
#ifdef EXPONENTIAL_DAMPING
    double rel_diff = fabs(new_rho - old_rho) / (fabs(old_rho) + 1e-99);
    double dampfact = 1 - exp(-rel_diff * 1);
    // use lambda about 0.95 with exponential damping
    dampfact *= param->lambda;
    dampfact -= math::Random(0.0, 1.0) * param->random;
    new_rho = damp(dampfact, old_rho, new_rho);
#endif

#ifdef SQUARED_DAMPING
    double v = fabs(old_rho) / (fabs(old_rho) + fabs(new_rho - old_rho));
    //double rel_diff = fabs(new_rho - old_rho) / (fabs(old_rho) + 1e-99);
    double dampfact = 1.0 - math::Sqr(v);
    // use lambda about 0.95 with exponential damping
    dampfact *= param->lambda;
    dampfact -= math::Random(0.0, 1.0) * param->random;
    new_rho = damp(dampfact, old_rho, new_rho);
#endif

    //fprintf(stderr, "%e %e %e\n", factor, sign, old_rho);

    bool now_exemplar = (new_rho > 0);

    squared_difference += math::Sqr(new_rho - old_rho);
    sum += new_rho;

    if (now_exemplar) {
      n_exemplars++;
    }

    point->info().rho = new_rho;
  }

  void Accumulate(const ApplyRhos& other) {
    n_changed += other.n_changed;
    n_exemplars += other.n_exemplars;
    squared_difference += other.squared_difference;
    squared_changed += other.squared_changed;
    sum += other.sum;
    hash ^= other.hash;
  }
};

/**
 * Statistics gatherer for per-iteration times for affinity propagation.
 */
class AffinityTimer {
 private:
  double last_alpha_micros_;
  double last_rho_micros_;
  double sum_times_;
  ArrayList<double> iteration_times_;
  
  static const int M = 1000000;

 public:
  void Init();
  void RecordTimes(timer *alpha_timer, timer *rho_timer);  
  void Report(datanode *module){} 
};

void AffinityTimer::Init() {
  iteration_times_.Init();
  last_alpha_micros_ = 0;
  last_rho_micros_ = 0;
  sum_times_ = 0;
}

void AffinityTimer::RecordTimes(timer *alpha_timer, timer *rho_timer) {
  double alpha_micros = alpha_timer->total.micros;
  double rho_micros = rho_timer->total.micros;
  double elapsed_alpha = alpha_micros - last_alpha_micros_;
  double elapsed_rho = rho_micros - last_rho_micros_;
  double elapsed = elapsed_alpha + elapsed_rho;
  *iteration_times_.AddBack() = elapsed;
  sum_times_ += elapsed;
  last_alpha_micros_ = alpha_micros;
  last_rho_micros_ = rho_micros;
  fprintf(stderr, " -- time: %.3f = %.3f a + %.3f r; mean is %.3f\n",
      elapsed/M, elapsed_alpha/M, elapsed_rho/M,
      sum_times_/iteration_times_.size()/M);
}

/**
 * Affinity propagation.
 */
void AffinityMain(datanode *module, const char *gnp_name) {
  AffinityCommon::Param *param;
  AffinityTimer timestats;
  const int TREE_CHANNEL = 300;
  const int ALPHA_CHANNEL = 350;
  const int RHO_CHANNEL = 360;
  const int REDUCE_CHANNEL = 370;
  const int DONE_CHANNEL = 390;
  int conv_it = fx_param_int(module, "affinity/conv_it", 50);
  int conv_thresh = fx_param_int(module, "affinity/conv_thresh", 5);
  int stable_iterations = 0;
  int maxit = fx_param_int(module, "affinity/maxit", 1000);
  index_t n_points;

  if (!rpc::is_root()) {
    // turn off fastexec output
    fx_silence();
  }

  timestats.Init();

  param = new AffinityCommon::Param();
  param->Init(fx_submodule(module, gnp_name, gnp_name));

  ThorTree<AffinityCommon::Param,
      AffinityCommon::Point, AffinityCommon::Node> tree;
  DistributedCache alphas;
  DistributedCache rhos;

  // One thing to note: alpha and rho are never taking up
  // RAM at the same time!
  double alpha_mb = fx_param_double(module, "alpha/megs", 200);
  double rho_mb = fx_param_double(module, "rho/megs", 100);
  timer *timer_alpha = fx_timer(module, "all_alpha");
  timer *timer_rho = fx_timer(module, "all_rho");

  fx_timer_start(module, "read");
  DistributedCache *points_cache = new DistributedCache();
  n_points = thor::ReadPoints<AffinityCommon::Point>(
      *param, TREE_CHANNEL + 0, TREE_CHANNEL + 1,
      fx_submodule(module, "data", "data"), points_cache);
  fx_timer_stop(module, "read");

  AffinityCommon::Point example_point;
  CacheArray<AffinityCommon::Point>::GetDefaultElement(points_cache,
      &example_point);
  param->dim = example_point.vec().length();

  fx_timer_start(module, "tree");
  thor::CreateKdTree<AffinityCommon::Point, AffinityCommon::Node>(
      *param, TREE_CHANNEL + 2, TREE_CHANNEL + 3,
      fx_submodule(module, "tree", "tree"), n_points, points_cache, &tree);
  fx_timer_stop(module, "tree");

  AffinityAlpha::QResult alpha_default;
  alpha_default.Init(*param);
  tree.CreateResultCache(ALPHA_CHANNEL, alpha_default,
      alpha_mb, &alphas);
  AffinityRho::QResult rho_default;
  rho_default.Init(*param);
  tree.CreateResultCache(RHO_CHANNEL, rho_default,
      rho_mb, &rhos);

  for (int iter = 0;;) {
    iter++;

    fx_timer_start(module, "all_alpha");
    thor::RpcDualTree<AffinityAlpha, DualTreeDepthFirst<AffinityAlpha> >(
        fx_submodule(module, "gnp", "iter/%d/alpha", iter), 200,
        *param, &tree, &tree, &alphas, NULL);
    ApplyAlphas apply_alphas;
    apply_alphas.Init(param);
    tree.Update<AffinityAlpha::QResult>(&alphas, &apply_alphas);
    rpc::Reduce(REDUCE_CHANNEL+0, VisitorReductor<ApplyAlphas>(), &apply_alphas);
    if (rpc::is_root()) {
      fprintf(stderr, ANSI_RED"--- %3d: alpha: max1=%.2e, max2=%.2e, netsim=%f"ANSI_CLEAR"\n",
          iter, apply_alphas.sum_alpha1 / n_points,
          apply_alphas.sum_alpha2 / n_points,
          apply_alphas.netsim / n_points);
    }
    alphas.ResetElements();
    fx_timer_stop(module, "all_alpha");

    fx_timer_start(module, "all_rho");
    thor::RpcDualTree<AffinityRho, DualTreeDepthFirst<AffinityRho> >(
        fx_submodule(module, "gnp", "iter/%d/rho", iter), 200,
        *param, &tree, &tree, &rhos, NULL);
    ApplyRhos apply_rhos;
    apply_rhos.Init(param);
    tree.Update<AffinityRho::QResult>(&rhos, &apply_rhos);
    rpc::Reduce(REDUCE_CHANNEL+1, VisitorReductor<ApplyRhos>(), &apply_rhos);
    if (rpc::is_root()) {
      fprintf(stderr, ANSI_GREEN"--- %3d:  rho: %"LI"d exemplars (%"LI"d changed, rms diff=%.1e, for c=%.1e, hash=%"LI"d)"ANSI_CLEAR"\n",
          iter, apply_rhos.n_exemplars, apply_rhos.n_changed,
          sqrt(apply_rhos.squared_difference / n_points),
          sqrt(apply_rhos.squared_changed / apply_rhos.n_changed),
          apply_rhos.hash);
    }
    rhos.ResetElements();
    fx_timer_stop(module, "all_rho");

    if (rpc::is_root()) {
      timestats.RecordTimes(timer_alpha, timer_rho);
    }

    Broadcaster<bool> done;

    if (rpc::is_root()) {
      // TODO: Better termination condition
      if (apply_rhos.n_changed < conv_thresh) {
        stable_iterations++;
      } else {
        stable_iterations = 0;
      }
      done.SetData(iter >= maxit || stable_iterations >= conv_it);
    }

    done.Doit(DONE_CHANNEL);

    if (done.get()) {
      break;
    }
  }

  timestats.Report(module);

  if (fx_param_bool(module, "brute_netsim", false)) {
    // Calculating netsim using brute force
    fprintf(stderr, "calculating netsim\n");
    CacheArray<AffinityCommon::Point> points_array;
    double netsim = 0;
    points_array.Init(&tree.points(), BlockDevice::M_READ);
    CacheReadIter<AffinityCommon::Point> r(&points_array, 0);
    ArrayList<Vector> list;
    list.Init();
    for (index_t j = 0; j < n_points; j++) {
      if (r->info().rho > 0) {
        list.AddBack()->Copy(r->vec());
        netsim += param->pref;
      }
      r.Next();
    }
    CacheReadIter<AffinityCommon::Point> q(&points_array, 0);
    for (index_t i = 0; i < n_points; i++) {
      double min_dist_sq = DBL_MAX;
      for (index_t j = 0; j < list.size(); j++) {
        double dist_sq = la::DistanceSqEuclidean(q->vec(), list[j]);
        if (dist_sq < min_dist_sq) {
          min_dist_sq = dist_sq;
        }
      }
      netsim -= min_dist_sq;
      
      q.Next();
    }
    fprintf(stderr, "netsim = %f\n", netsim / n_points);
    fx_format_result(module, "netsim", "%.5e", netsim / n_points);
  }

  delete param;
}

int main(int argc, char *argv[]) {
  fx_init(argc, argv);
  rpc::Init();

  srand(time(NULL));

  AffinityMain(fx_root, "affinity");

  rpc::Done();
  fx_done();
}


#if 0
// The old source code!

//
//  AffinityAlpha::Param param;
//
//  param.Init(fx_submodule(module, gnp_name, gnp_name));
//
//  TempCacheArray<AffinityAlpha::QPoint> data_points;
//  TempCacheArray<AffinityAlpha::QNode> data_nodes;
//
//  index_t n_block_points = fx_param_int(
//      module, "n_block_points", 1024);
//  index_t n_block_nodes = fx_param_int(
//      module, "n_block_nodes", 128);
//
//  datanode *data_module = fx_submodule(module, "data", "data");
//
//  fx_timer_start(data_module, "read");
//
//  Matrix data_matrix;
//  MUST_PASS(data::Load(fx_param_str_req(data_module, ""), &data_matrix));
//  index_t n_points = data_matrix.n_cols();
//  index_t dimensionality = data_matrix.n_rows();
//  AffinityAlpha::QPoint default_point;
//  default_point.vec().Init(data_matrix.n_rows());
//  param.BootstrapMonochromatic(&default_point, data_matrix.n_cols());
//  data_points.Init(default_point, n_points, n_block_points);
//  for (index_t i = 0; i < data_matrix.n_cols(); i++) {
//    AffinityAlpha::QPoint *point = &data_points[i];
//    point->vec().CopyValues(data_matrix.GetColumnPtr(i));
//  }
//  FindCovariance(data_matrix);
//  data_matrix.Destruct();
//  data_matrix.Init(0, 0);
//
//  fx_timer_stop(data_module, "read");
//
//  AffinityAlpha::QNode data_example_node;
//  data_example_node.Init(dimensionality, param);
//
//  // Initial conditions for alpha
//  for (index_t i = 0; i < n_points; i++) {
//    CacheWrite<AffinityAlpha::QPoint> point(&data_points, i);
//    point->info().alpha.max1 = 0;
//    point->info().alpha.max2 = param.pref;
//    point->info().alpha.max1_index = i;
//    if (rand() % 8 == 0) {
//      point->info().rho = -param.pref / 2;
//    } else {
//      point->info().rho = 0;
//    }
//  }
//  data_nodes.Init(data_example_node, 0, n_block_nodes);
//  KdTreeHybridBuilder
//      <AffinityAlpha::QPoint, AffinityAlpha::QNode, AffinityAlpha::Param>
//      ::Build(data_module, param, &data_points, &data_nodes);
//
//
//  // ---------------------------------------------------------------
//  // The algorithm follows -----------------------------------------
//
//  // All the above is not any different for affinity than for anything
//  // else.  Now, time for the iteration.
//  int n_iter = fx_param_int(module, "n_iter", 10000);
//  int iter;
//  int n_convergence_iter = fx_param_int(module, "n_convergence_iter", 16);
//  int convergence_thresh = fx_param_int(module, "convergence_thresh", 8);
//  int stable_iter = 0;
//  index_t n_changed = n_points / 2;
//  index_t n_exemplars = 0;
//  timer *timer_rho = fx_timer(module, "all_rho");
//  timer *timer_alpha = fx_timer(module, "all_alpha");
//
//  ArrayList<char> is_exemplar;
//  is_exemplar.Init(n_points);
//  
//  ArrayList<double> iter_times_rho;
//  ArrayList<double> iter_times_alpha;
//  ArrayList<double> iter_times_total;
//  iter_times_rho.Init();
//  iter_times_alpha.Init();
//  iter_times_total.Init();
//
//  for (index_t i = 0; i < n_points; i++) {
//    is_exemplar[i] = 0;
//  }
//
//  for (iter = 0; iter < n_iter; iter++)
//  {
//    double lambda = param.lambda;
//    index_t n_alpha_changed = 0;
//    double sum_alpha = 0;
//    double sum_alpha2 = 0;
//    double sum_rho = 0;
//    index_t unclassifieds;
//    double last_rho_time = timer_rho->total.cpu.tms_utime;
//    double last_alpha_time = timer_alpha->total.cpu.tms_utime;
//
//    n_exemplars = 0;
//    unclassifieds = 0;
//
//    fx_timer_start(module, "all_alpha");
//    {
//      TempCacheArray<AffinityAlpha::QResult> q_results_alpha;
//
//      AffinityAlpha::QResult default_result_alpha;
//      default_result_alpha.Init(param);
//      q_results_alpha.Init(default_result_alpha, data_points.end_index(),
//          data_points.n_block_elems());
//
//      thor::ThreadedDualTreeSolver
//               < AffinityAlpha, DualTreeDepthFirst<AffinityAlpha> >::Solve(
//          fx_submodule(module, "threads", "iters/%d/alpha", iter), param,
//          &data_points, &data_nodes, &data_points, &data_nodes,
//          &q_results_alpha);
//
//      for (index_t i = 0; i < n_points; i++) {
//        AffinityAlpha::QPoint *point = &data_points[i];
//        AffinityAlpha::QResult *result = &q_results_alpha[i];
//
//        if (point->info().alpha.max1_index != result->alpha.max1_index) {
//          n_alpha_changed++;
//        }
//
//        point->info().alpha = result->alpha;
///
//        sum_alpha += result->alpha.max1;
//        sum_alpha2 += result->alpha.max2;
//
//        if (!is_exemplar[result->alpha.max1_index] && point->info().rho <= 0) {
//          unclassifieds++;
//        }
//      }
//
//      thor::StatFixer<
//          AffinityAlpha::Param, AffinityAlpha::QPoint, AffinityAlpha::QNode>
//          ::Fix(param, &data_points, &data_nodes);
//    }
//    fx_timer_stop(module, "all_alpha");
//
//    fprintf(stderr,
//        "\033[31m     -------- %04d alpha: sum_alpha = %f, sum_alpha2 = %f, %"LI"d alphas changed, %"LI"d unclassifieds, %.3fsec cum. alpha\033[0m\n",
//        iter, sum_alpha, sum_alpha2, n_alpha_changed, unclassifieds,
//        timer_alpha->total.micros / 1.0e6);
//
//    fx_timer_start(module, "all_rho");
//    {
//      double rho_squared_difference = 0;
//      TempCacheArray<AffinityRho::QResult> q_results_rho;
//
//      AffinityRho::QResult default_result_rho;
//      default_result_rho.Init(param);
//      q_results_rho.Init(default_result_rho, data_points.end_index(),
//          data_points.n_block_elems());
//
//      thor::ThreadedDualTreeSolver< AffinityRho, DualTreeDepthFirst<AffinityRho> >::Solve(
//          fx_submodule(module, "threads", "iters/%d/rho", iter), param,
//          &data_points, &data_nodes, &data_points, &data_nodes,
//          &q_results_rho);
//
//      n_changed = 0;
//
//      for (index_t i = 0; i < n_points; i++) {
//        AffinityAlpha::QPoint *point = &data_points[i];
//        AffinityRho::QResult *result = &q_results_rho[i];
//        double old_rho = point->info().rho;
//        double new_rho = damp(lambda, old_rho, result->rho);
//
//        if ((old_rho > 0) != (new_rho > 0)) {
//          new_rho *= math::Random(0.4, 1.4);
//          n_changed++;
//        }
//
//        rho_squared_difference += math::Sqr(new_rho - old_rho);
//        sum_rho += new_rho;
//
//        if (new_rho > 0) {
//          is_exemplar[i] = 1;
//          n_exemplars++;
//        } else {
//          is_exemplar[i] = 0;
//        }
//
//        point->info().rho = new_rho;
//      }
//
//
//      thor::StatFixer<
//          AffinityAlpha::Param, AffinityAlpha::QPoint, AffinityAlpha::QNode>
//          ::Fix(param, &data_points, &data_nodes);
//
//      param.SetEpsilon(sqrt(rho_squared_difference / n_points));
//      //param.SetEpsilon(0);
//    }
//    fx_timer_stop(module, "all_rho");
//
//    fprintf(stderr, "\033[32m------------- %04d rho: %"LI"d rhos changed, (%"LI"d exemplars), sum_rho = %f, eps = %f, %.3fsec cum. rho\033[0m\n",
//        iter, n_changed, n_exemplars,
//        sum_rho, param.eps,
//        timer_rho->total.micros / 1.0e6);
//
//    *iter_times_rho.AddBack() = timer_rho->total.cpu.tms_utime - last_rho_time;
//    *iter_times_alpha.AddBack() = timer_alpha->total.cpu.tms_utime - last_alpha_time;
//    *iter_times_total.AddBack() = (timer_rho->total.cpu.tms_utime - last_rho_time)
//        + (timer_alpha->total.cpu.tms_utime - last_alpha_time);
//
//    if (n_changed < convergence_thresh) {
//      stable_iter++;
//    } else {
//      stable_iter = 0;
//    }
//    
//    if (stable_iter >= n_convergence_iter && iter > 20) {
//      break;
//    }
//  }
//
//  fx_format_result(module, "n_iterations", "%d", iter);
//  fx_format_result(module, "n_exemplars", "%d", n_exemplars);
//
//  TimeStats(fx_submodule(module, "iter_times_rho", "iter_times_rho"),
//      iter_times_rho);
//  TimeStats(fx_submodule(module, "iter_times_alpha", "iter_times_alpha"),
//      iter_times_alpha);
//  TimeStats(fx_submodule(module, "iter_times_total", "iter_times_total"),
//      iter_times_total);
//  fx_format_result(module, "n_points", "%"LI"d", n_points);
//
//  // This will take too long if there are too many exemplars.
//  if (n_exemplars >= 10000) {
//    NONFATAL("Too many exemplars (%"LI"d >= 10000), NOT performing clustering.\n",
//        n_exemplars);
//  } else {
//    FindExemplars(module, param, dimensionality, n_points, &data_points);
//  }
//}
//
///*
//void FindExemplars(datanode *module, const AffinityCommon::Param& param,
//    index_t dimensionality, index_t n_points,
//    CacheArray<AffinityAlpha::QPoint> *data_points) {
//  fx_timer_start(module, "exemplars");
//
//  ArrayList<Cluster> clusters;
//  ArrayList<index_t> assignments;
//  CacheReadIter<AffinityAlpha::QPoint> point(data_points, 0);
//
//  clusters.Init();
//
//  for (index_t point_i = 0; point_i < n_points; point_i++, point.Next()) {
//    if (point->info().rho > 0) {
//      Cluster *cluster = clusters.AddBack();
//      cluster->exemplar.Copy(point->vec());
//      cluster->centroid.Init(dimensionality);
//      cluster->centroid.SetZero();
//      cluster->count = 0;
//    }
//  }
//
//  // Run all nearest neighbors to assign clusters and find centroids
//  assignments.Init(n_points);
//
//  point.SetIndex(0);
//  for (index_t point_i = 0; point_i < n_points; point_i++, point.Next()) {
//    double best_distsq = DBL_MAX;
//    index_t best_k = -1;
//
//    for (index_t k = 0; k < clusters.size(); k++) {
//      double distsq = la::DistanceSqEuclidean(
//          clusters[k].exemplar, point->vec());
//      if (unlikely(distsq < best_distsq)) {
//        best_distsq = distsq;
//        best_k = k;
//      }
//    }
//
//    assignments[point_i] = best_k;
//    clusters[best_k].count++;
//    la::AddTo(point->vec(), &clusters[best_k].centroid);
//  }
//
//  // Divide centroids by size
//  for (index_t i = 0; i < clusters.size(); i++) {
//    la::Scale(1.0 / clusters[i].count, &clusters[i].centroid);
//  }
//
//  // Calculate net similarity
//  double netsim = param.pref * clusters.size();
//
//  point.SetIndex(0);
//  for (index_t point_i = 0; point_i < n_points; point_i++, point.Next()) {
//    Cluster *cluster = &clusters[assignments[point_i]];
//    double distsq = la::DistanceSqEuclidean(cluster->exemplar, point->vec());
//    netsim -= distsq;
//  }
//
//  fx_timer_start(module, "exemplars");
//  fx_format_result(module, "netsim", "%f", netsim);
//
//  // Make a matrix of exemplars so we can save it to file
//  Matrix m;
//  m.Init(dimensionality, clusters.size());
//
//  for (index_t i = 0; i < clusters.size(); i++) {
//    Vector dest;
//    m.MakeColumnVector(i, &dest);
//    dest.CopyValues(clusters[i].exemplar);
//  }
//
//  data::Save("exemplars.txt", m);
//}
//
//void FindCovariance(const Matrix& dataset) {
//  Matrix m;
//  Vector sums;
//
//  m.Init(dataset.n_rows(), dataset.n_cols());
//  sums.Init(dataset.n_rows());
//  sums.SetZero();
//
//  for (index_t i = 0; i < dataset.n_cols(); i++) {
//    Vector s;
//    Vector d;
//    dataset.MakeColumnSubvector(i, 0, dataset.n_rows(), &s);
//    m.MakeColumnVector(i, &d);
//    d.CopyValues(s);
//    la::AddTo(s, &sums);
//  }
//
//  la::Scale(-1.0 / dataset.n_cols(), &sums);
//  for (index_t i = 0; i < dataset.n_cols(); i++) {
//    Vector d;
//    m.MakeColumnVector(i, &d);
//    la::AddTo(sums, &d);
//  }
//
//  Matrix cov;
//
//  la::MulTransBInit(m, m, &cov);
//  la::Scale(1.0 / (dataset.n_cols() - 1), &cov);
//
//  cov.PrintDebug("covariance");
//
//  Vector d;
//  Matrix u; // eigenvectors
//  Matrix ui; // the inverse of eigenvectors
//
//  la::EigenvectorsInit(cov, &d, &u);
//  d.PrintDebug("covariance_eigenvectors");
//  la::TransposeInit(u, &ui);
//
//  for (index_t i = 0; i < d.length(); i++) {
//    d[i] = 1.0 / sqrt(d[i]);
//  }
//
//  la::ScaleRows(d, &ui);
//
//  Matrix cov_inv_half;
//  la::MulInit(u, ui, &cov_inv_half);
//
//  Matrix final;
//  la::MulInit(cov_inv_half, m, &final);
//
//  for (index_t i = 0; i < dataset.n_cols(); i++) {
//    Vector s;
//    Vector d;
//    dataset.MakeColumnSubvector(i, 0, dataset.n_rows()-1, &d);
//    final.MakeColumnVector(i, &s);
//    d.CopyValues(s);
//  }
//}
//
//inline double damp(double lambda, double prev, double next) {
//  return lambda * prev + (1 - lambda) * next;
//}
//
//void TimeStats(datanode *module, const ArrayList<double>& list) {
//  double v_avg;
//  MinHeap<double, char> heap;
//  double f = 1.0 / sysconf(_SC_CLK_TCK);
//
//  heap.Init();
//  v_avg = 0;
//  for (index_t i = 0; i < list.size(); i++) {
//    v_avg += list[i];
//    heap.Put(list[i], 0);
//  }
//  v_avg /= list.size();
//
//  double v_min;
//  double v_med;
//  double v_max;
//
//  v_min = heap.top_key();
//  while (heap.size() > list.size() / 2) {
//    heap.PopOnly();
//  }
//  v_med = heap.top_key();
//  while (heap.size() > 1) {
//    heap.PopOnly();
//  }
//  v_max = heap.top_key();
//
//  fx_format_result(module, "min", "%f", v_min*f);
//  fx_format_result(module, "med", "%f", v_med*f);
//  fx_format_result(module, "max", "%f", v_max*f);
//  fx_format_result(module, "avg", "%f", v_avg*f);
//  fx_format_result(module, "sum", "%f", v_avg*list.size()*f);
//}
//*/
#endif
