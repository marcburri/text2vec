#include "text2vec.h"
using namespace RcppParallel;
using namespace Rcpp;
using namespace std;

class GloveFit {
public:
  GloveFit(size_t vocab_size,
           size_t word_vec_size,
           double learning_rate,
           int x_max,
           double max_cost,
           double alpha):
  vocab_size(vocab_size), word_vec_size(word_vec_size),
  x_max(x_max), learning_rate(learning_rate), max_cost(max_cost), alpha(alpha) {

    w_i.resize(vocab_size);
    w_j.resize(vocab_size);
    grad_sq_w_i.resize(vocab_size);
    grad_sq_w_j.resize(vocab_size);

    for(size_t i = 0; i < vocab_size; i++) {
      w_i[i].resize(word_vec_size);
      w_j[i].resize(word_vec_size);
      grad_sq_w_i[i].resize(word_vec_size);
      grad_sq_w_j[i].resize(word_vec_size);
    }
    fill_mat_rand(w_i, word_vec_size, -0.5, 0.5);
    fill_mat_rand(w_j, word_vec_size, -0.5, 0.5);
    fill_mat_val(grad_sq_w_i, word_vec_size, 1.0);
    fill_mat_val(grad_sq_w_j, word_vec_size, 1.0);

    b_i.resize(vocab_size);
    b_j.resize(vocab_size);
    grad_sq_b_i.resize(vocab_size);
    grad_sq_b_j.resize(vocab_size);
    fill_vec_rand(b_i, -0.5, 0.5);
    fill_vec_rand(b_j, -0.5, 0.5);
    fill_vec_val(grad_sq_b_i, 1.0);
    fill_vec_val(grad_sq_b_j, 1.0);
  };

  inline double weighting_fun(double x, double x_max, double alpha) {
    if(x < x_max)
      return pow(x / x_max, alpha);
    else
      return 1.0;
  }

  inline double adagrad_iterate( size_t begin,
                                 size_t end,
                                 const RVector<int> &x_irow,
                                 const RVector<int> &x_icol,
                                 const RVector<double> &x_val) {

    double global_cost = 0, weight, cost_inner, wcost, cost;
    double grad_b_i, grad_b_j, grad_k_i, grad_k_j;
    size_t x_irow_i, x_icol_i;
    for (size_t i = begin; i < end; i++) {
      x_irow_i = x_irow[i];
      x_icol_i = x_icol[i];
      weight = weighting_fun(x_val[i], x_max, this->alpha);

      cost_inner = inner_product(w_i[ x_irow_i ].begin(), w_i[ x_irow_i ].end() ,
                                        w_j[ x_icol_i].begin(),
                                        // init with (b_i + b_j - log(x_ij))
                                        b_i[ x_irow_i ] + b_j[ x_icol_i ] - log( x_val[i] ) );
      //clip cost for numerical stability
      if (cost_inner > this->max_cost)
        cost_inner = max_cost;
      else if (cost_inner < -(this->max_cost))
        cost_inner = -max_cost;

      wcost = weight * cost_inner;

      cost = wcost * cost_inner;

      global_cost += 0.5 * cost;

      //Compute gradients for bias terms
      grad_b_i = wcost;
      grad_b_j = wcost;

      // Compute gradients for word vector terms.
      for (int k = 0; k < word_vec_size; k++) {

        grad_k_i = wcost * w_j[ x_icol_i ][ k ];
        grad_k_j = wcost * w_i[ x_irow_i ][ k ];

        // Perform adaptive updates for word vectors
        w_i[ x_irow_i ][ k ] -= (learning_rate * grad_k_i / sqrt( grad_sq_w_i[ x_irow_i ][ k ] ) );
        w_j[ x_icol_i ][ k ] -= (learning_rate * grad_k_j / sqrt( grad_sq_w_j[ x_icol_i ][ k ] ) );

        // Update squared gradient sums for word vectors
        grad_sq_w_i[ x_irow_i ][ k ] += grad_k_i * grad_k_i;
        grad_sq_w_j[ x_icol_i ][ k ] += grad_k_j * grad_k_j;
      }

      // Perform adaptive updates for bias terms
      b_i[ x_irow_i ] -= (learning_rate * grad_b_i / sqrt( grad_sq_b_i[ x_irow_i ] ));
      b_j[ x_icol_i ] -= (learning_rate * grad_b_j / sqrt( grad_sq_b_j[ x_icol_i ] ));

      // Update squared gradient sums for biases
      grad_sq_b_i[ x_irow_i ] += grad_b_i * grad_b_i;
      grad_sq_b_j[ x_icol_i ] += grad_b_j * grad_b_j;
    }
    return global_cost;
  }

  List get_word_vectors() {
    return List::create(_["w_i"] = convert2Rmat(this->w_i, this->word_vec_size),
                        _["w_j"] = convert2Rmat(this->w_j, this->word_vec_size));
  }

  List get_fit() {
    return List::create(_["w_i"] = convert2Rmat(this->w_i, this->word_vec_size),
                        _["w_j"] = convert2Rmat(this->w_j, this->word_vec_size),
                        _["grad_sq_w_i"] = convert2Rmat(this->grad_sq_w_i, this->word_vec_size),
                        _["grad_sq_w_j"] = convert2Rmat(this->grad_sq_w_j, this->word_vec_size),
                        _["b_i"] = this->b_i,
                        _["b_j"] = this->b_j,
                        _["grad_sq_b_i"] = this->grad_sq_b_i,
                        _["grad_sq_b_j"] = this->grad_sq_b_j,
                        _["vocab_size"] = this->vocab_size,
                        _["word_vec_size"] = this->word_vec_size,
                        _["x_max"] = this->x_max,
                        _["learning_rate"] = this->learning_rate
    );
  }

private:
  int vocab_size;
  int word_vec_size;
  int x_max;
  double learning_rate;
  // see https://github.com/maciejkula/glove-python/pull/9#issuecomment-68058795
  // clips the cost for numerical stability
  double max_cost;
  double alpha;
  // word vecrtors
  vector<vector<double> > w_i;
  vector<vector<double> > w_j;
  // word biases
  vector<double> b_i;
  vector<double> b_j;
  // word vectors square gradients
  vector<vector<double> > grad_sq_w_i;
  vector<vector<double> > grad_sq_w_j;
  // word biases square gradients
  vector<double> grad_sq_b_i;
  vector<double> grad_sq_b_j;
};
