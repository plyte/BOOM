#include "gtest/gtest.h"

#include "test_utils/test_utils.hpp"

#include "cpputil/math_utils.hpp"

#include "Models/ChisqModel.hpp"
#include "Models/MvnModel.hpp"
#include "Models/MvnGivenScalarSigma.hpp"
#include "Models/PosteriorSamplers/IndependentMvnVarSampler.hpp"
#include "Models/PosteriorSamplers/ZeroMeanGaussianConjSampler.hpp"

#include "Models/Glm/MvnGivenX.hpp"
#include "Models/Glm/PosteriorSamplers/BregVsSampler.hpp"
#include "Models/Glm/PosteriorSamplers/RegressionSemiconjugateSampler.hpp"
#include "Models/Glm/PosteriorSamplers/IndependentRegressionModelsPosteriorSampler.hpp"

#include "Models/StateSpace/MultivariateStateSpaceRegressionModel.hpp"
#include "Models/StateSpace/StateModels/LocalLevelStateModel.hpp"
#include "Models/StateSpace/StateModels/SeasonalStateModel.hpp"
#include "Models/StateSpace/PosteriorSamplers/SharedLocalLevelPosteriorSampler.hpp"
#include "Models/StateSpace/PosteriorSamplers/MvStateSpaceRegressionPosteriorSampler.hpp"
#include "Models/StateSpace/PosteriorSamplers/StateSpacePosteriorSampler.hpp"

#include "distributions.hpp"
#include "LinAlg/Array.hpp"

namespace {

  using namespace BOOM;
  using std::endl;
  using std::cout;

  class MultivariateStateSpaceRegressionModelTest : public ::testing::Test {
   protected:
    MultivariateStateSpaceRegressionModelTest()
    {
      GlobalRng::rng.seed(8675310);
    }
  };

  //===========================================================================
  TEST_F(MultivariateStateSpaceRegressionModelTest, EmptyTest) {}

  //===========================================================================
  TEST_F(MultivariateStateSpaceRegressionModelTest, ConstructorTest) {
    MultivariateStateSpaceRegressionModel model(3, 4);
  }

  TEST_F(MultivariateStateSpaceRegressionModelTest, DataTest) {
    TimeSeriesRegressionData data_point(3.2, Vector{1, 2, 3}, 0, 4);
    EXPECT_DOUBLE_EQ(3.2, data_point.y());
    EXPECT_TRUE(VectorEquals(Vector{1, 2, 3}, data_point.x()));
    EXPECT_EQ(0, data_point.series());
    EXPECT_EQ(4, data_point.timestamp());
  }

  TEST_F(MultivariateStateSpaceRegressionModelTest, ModelTest) {
    int ydim = 4;
    int xdim = 3;
        
    MultivariateStateSpaceRegressionModel model(xdim, ydim);
    EXPECT_EQ(0, model.state_dimension());
    EXPECT_EQ(0, model.number_of_state_models());
    EXPECT_EQ(nullptr, model.state_model(0));
    EXPECT_EQ(nullptr, model.state_model(-1));
    EXPECT_EQ(nullptr, model.state_model(2));
    EXPECT_EQ(0, model.time_dimension());

    EXPECT_EQ(ydim, model.nseries());
    EXPECT_EQ(xdim, model.xdim());

    std::vector<Ptr<TimeSeriesRegressionData>> data;
    Matrix response_data(ydim, 12);
    for (int time = 0; time < 12; ++time) {
      for (int series = 0; series < ydim; ++series){
        NEW(TimeSeriesRegressionData, data_point)(
            rnorm(0, 1), rnorm_vector(xdim, 0, 1), series, time);
        data.push_back(data_point);
        model.add_data(data_point);
        response_data(series, time) = data_point->y();
      }
    }
    EXPECT_EQ(12, model.time_dimension());
    for (int time = 0; time < 12; ++time) {
      for (int series = 0; series < ydim; ++series) {
        EXPECT_TRUE(model.is_observed(series, time));
        EXPECT_DOUBLE_EQ(response_data(series, time),
                         model.observed_data(series, time));
      }
    }
  }

  TEST_F(MultivariateStateSpaceRegressionModelTest, McmcTest) {
    // Simulate fake data from the model: shared local level and a regression
    // effect.

    int xdim = 3;
    int nseries = 6;
    int nfactors = 2;
    int sample_size = 100;
    double factor_sd = .3;
    double residual_sd = .1;

    //----------------------------------------------------------------------
    // Simulate the state.
    Matrix state(nfactors, sample_size);
    for (int factor = 0; factor < nfactors; ++factor) {
      state(factor, 0) = rnorm();
      for (int time = 1; time < sample_size; ++time) {
        state(factor, time) = state(factor, time - 1) + rnorm(0, factor_sd);
      }
    }

    // Set up the observation coefficients, which are zero above the diagonal
    // and 1 on the diagonal.
    Matrix observation_coefficients(nseries, nfactors);
    observation_coefficients.randomize();
    for (int i = 0; i < nfactors; ++i) {
      observation_coefficients(i, i) = 1.0;
      for (int j = i + 1; j < nfactors; ++j) {
        observation_coefficients(i, j) = 0.0;
      }
    }

    // Set up the regression coefficients and the predictors.
    Matrix regression_coefficients(nseries, xdim);
    regression_coefficients.randomize();
    Matrix predictors(sample_size, xdim);
    predictors.randomize();

    // Simulate the response.
    Matrix response(sample_size, nseries);
    for (int i = 0; i < sample_size; ++i) {
      Vector yhat = observation_coefficients * state.col(i)
          + regression_coefficients * predictors.row(i);
      for (int j = 0; j < nseries; ++j) {
        response(i, j) = yhat[j] + rnorm(0, residual_sd);
      }
    }

    //----------------------------------------------------------------------
    // Define the model.
    NEW(MultivariateStateSpaceRegressionModel, model)(xdim, nseries);
    for (int time = 0; time < sample_size; ++time) {
      for (int series = 0; series < nseries; ++series) {
        NEW(TimeSeriesRegressionData, data_point)(
            response(time, series), predictors.row(time), series, time);
        model->add_data(data_point);
      }
    }
    EXPECT_EQ(sample_size, model->time_dimension());
    
    //---------------------------------------------------------------------------
    // Define the state model.
    NEW(SharedLocalLevelStateModel, state_model)(
        nfactors, model.get(), nseries);
    std::vector<Ptr<GammaModelBase>> innovation_precision_priors;
    for (int factor = 0; factor < nfactors; ++factor) {
      innovation_precision_priors.push_back(new ChisqModel(1.0, .10));
    }
    Matrix observation_coefficient_prior_mean(nseries, nfactors, 0.0);

    NEW(MvnModel, slab)(Vector(nfactors, 0.0), SpdMatrix(nfactors, 1.0));
    NEW(VariableSelectionPrior, spike)(nfactors, 1.0);
    NEW(SharedLocalLevelPosteriorSampler, state_model_sampler)(
        state_model.get(),
        std::vector<Ptr<MvnBase>>(nseries, slab),
        std::vector<Ptr<VariableSelectionPrior>>(nseries, spike));
    state_model->set_method(state_model_sampler);
    state_model->set_initial_state_mean(Vector(nfactors, 0.0));
    state_model->set_initial_state_variance(SpdMatrix(nfactors, 1.0));
    model->add_state(state_model);

    //---------------------------------------------------------------------------
    // Set the prior and sampler for the regression model.
    for (int i = 0; i < nseries; ++i) {
      Vector beta_prior_mean(xdim, 0.0);
      SpdMatrix beta_precision(xdim, 1.0);
      NEW(MvnModel, beta_prior)(beta_prior_mean, beta_precision, true);
      NEW(ChisqModel, residual_precision_prior)(1.0, residual_sd);
      NEW(RegressionSemiconjugateSampler, regression_sampler)(
          model->observation_model()->model(i).get(),
          beta_prior, residual_precision_prior);
      model->observation_model()->model(i)->set_method(regression_sampler);
    }

    NEW(MultivariateStateSpaceRegressionPosteriorSampler, sampler)(
        model.get());
    model->set_method(sampler);
    int niter = 500;
    Matrix factor0_draws(niter, sample_size);
    Matrix factor1_draws(niter, sample_size);
    for (int i = 0; i < niter; ++i) {
      model->sample_posterior();
      factor0_draws.row(i) = model->shared_state().row(0);
      factor1_draws.row(i) = model->shared_state().row(1);
    }

    CheckMcmcMatrix(factor0_draws, state.row(0));
    CheckMcmcMatrix(factor1_draws, state.row(1));
    //    std::ofstream("factor0.txt") << rbind(state.row(0), factor0_draws);
    //    std::ofstream("factor1.txt") << rbind(state.row(1), factor1_draws);
  }

  //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  
  // A test case with both shared state and a single series that has series
  // specific state (in this case a seasonal model).
  TEST_F(MultivariateStateSpaceRegressionModelTest, SharedPlusIndividualTest) {
    int xdim = 3;
    int nseries = 8;
    int nfactors = 3;
    int sample_size = 250;

    int special_series = 4;
    int nseasons = 7;
    double factor_sd = 1.0;
    double seasonal_innovation_sd = .2;
    double residual_sd = .1;

    //----------------------------------------------------------------------
    // Simulate the shared state.
    //----------------------------------------------------------------------
    Matrix state(nfactors, sample_size, 0.0);
    for (int factor = 0; factor < nfactors; ++factor) {
      state(factor, 0) = rnorm();
      for (int time = 1; time < sample_size; ++time) {
        state(factor, time) = state(factor, time - 1) + rnorm(0, factor_sd);
      }
    }
    
    Matrix observation_coefficients(nseries, nfactors);
    observation_coefficients.randomize();
    observation_coefficients.diag() = 1.0;
    for (int i = 0; i < std::min<int>(nrow(observation_coefficients),
                                      ncol(observation_coefficients)); ++i) {
      for (int j = i + 1; j < ncol(observation_coefficients); ++j) {
        observation_coefficients(i, j) = 0.0;
      }
    }

    // The columns of state_contribution are time points.  The rows are series.
    Matrix state_contribution = (observation_coefficients * state).transpose();
    
    //----------------------------------------------------------------------
    // Simulate the regression component.
    //----------------------------------------------------------------------
    Matrix predictors(sample_size * nseries, xdim);
    predictors.randomize();
    predictors.col(0) = 1.0;

    Matrix regression_coefficients(nseries, xdim);
    regression_coefficients.randomize();

    //----------------------------------------------------------------------
    // Simulate a seasonal pattern for one of the series.
    //----------------------------------------------------------------------
    Vector seasonal_pattern = rnorm_vector(7 - 1, 0, 20);
    SeasonalStateSpaceMatrix seasonal_matrix(nseasons);
    Vector seasonal(sample_size);
    for (int i = 0; i < sample_size; ++i) {
      seasonal[i] = seasonal_pattern[0];
      seasonal_pattern = seasonal_matrix * ConstVectorView(seasonal_pattern);
      seasonal_pattern[0] += rnorm(0, seasonal_innovation_sd);
    }
    
    //----------------------------------------------------------------------
    // Simulate errors, and add them to get responses.
    //----------------------------------------------------------------------
    Vector errors = rnorm_vector(nseries * sample_size, 0, residual_sd);
    
    //==========================================================================
    // Construct the model
    //==========================================================================

    NEW(MultivariateStateSpaceRegressionModel, model)(xdim, nseries);
    
    //----------------------------------------------------------------------
    // Add data
    //----------------------------------------------------------------------
    int index = -1;
    for (int time = 0; time < sample_size; ++time) {
      for (int series = 0; series < nseries; ++series) {
        ++index;
        double regression = predictors.row(index).dot(
            regression_coefficients.row(series));
        double y = regression + errors[index]
            + observation_coefficients.row(series).dot(state.col(time));
        NEW(TimeSeriesRegressionData, data_point)(
            y, predictors.row(index), series, time);
        model->add_data(data_point);
      }
    }
    
    //----------------------------------------------------------------------
    // Add state models
    //----------------------------------------------------------------------
    NEW(SharedLocalLevelStateModel, state_model)(nfactors, model.get(), nseries);

    // Add posterior sampler for state model.
    std::vector<Ptr<VariableSelectionPrior>> spikes;
    std::vector<Ptr<MvnBase>> slabs;
    for (int i = 0; i < model->nseries(); ++i) {
      Vector inc_probs(nfactors, 1.0);
      for (int j = i + 1; j < inc_probs.size(); ++j) {
        inc_probs[j] = 0.0;
      }
      NEW(VariableSelectionPrior, spike)(inc_probs);
      spikes.push_back(spike);

      NEW(MvnGivenXMvRegSuf, slab)(
          new VectorParams(Vector(nfactors, 1.0)),
          new UnivParams(1.0),
          Vector(),
          .5,
          state_model->coefficient_model()->suf());
      slabs.push_back(slab);
    }
    NEW(SharedLocalLevelPosteriorSampler, state_model_sampler)(
        state_model.get(), slabs, spikes);
    state_model->set_observation_coefficients(observation_coefficients);
    state_model->set_method(state_model_sampler);
    
    // Add the initial distribution for the state model.
    state_model->set_initial_state_mean(Vector(nfactors, 0.0));
    state_model->set_initial_state_variance(SpdMatrix(nfactors, 100.0));

    model->add_state(state_model);

    //----------------------------------------------------------------------
    // Add a series specific state model for the special series.
    //----------------------------------------------------------------------
    Ptr<SeasonalStateModel> seasonal_model(new SeasonalStateModel(nseasons, 1));
    seasonal_model->set_sigsq(square(seasonal_innovation_sd));

    seasonal_model->set_initial_state_mean(
        Vector(seasonal_model->state_dimension(), 0.0));
    seasonal_model->set_initial_state_variance(100);

    NEW(ZeroMeanGaussianConjSampler, seasonal_model_sampler)(
        seasonal_model.get(), 1, seasonal_innovation_sd);
    seasonal_model->set_method(seasonal_model_sampler);

    model->add_series_specific_state(seasonal_model, special_series);

    //----------------------------------------------------------------------
    // Add Samplers for the observation model
    //----------------------------------------------------------------------
    for (int series = 0; series < model->nseries(); ++series) {
      Ptr<RegressionModel> series_reg =
          model->observation_model()->model(series);
      model->observation_model()->model(series)->set_Beta(
          regression_coefficients.row(series));
      model->observation_model()->model(series)->set_sigsq(square(residual_sd));

      NEW(MvnGivenScalarSigma, slab)(Vector(xdim, 0.0),
                                     SpdMatrix(xdim, 1.0 / 10000.0),
                                     series_reg->Sigsq_prm());
      Vector prior_inclusion_probabilities(xdim, 0.5);
      NEW(VariableSelectionPrior, spike)(prior_inclusion_probabilities);
      NEW(ChisqModel, residual_precision_prior)(10000000, residual_sd);
      NEW(BregVsSampler, reg_sampler)(
          series_reg.get(), slab, residual_precision_prior, spike);
      series_reg->set_method(reg_sampler);
    }
    NEW(IndependentRegressionModelsPosteriorSampler, observation_model_sampler)(
        model->observation_model());
    model->observation_model()->set_method(observation_model_sampler);

    //---------------------------------------------------------------------------
    // Check that the proxy model has the correct model matrices.
    //---------------------------------------------------------------------------
    EXPECT_DOUBLE_EQ(square(residual_sd),
                     model->series_specific_model(
                         special_series)->observation_variance(2));

    StateSpaceModel temp_model;
    temp_model.add_state(seasonal_model);

    EXPECT_TRUE(MatrixEquals(
        temp_model.state_transition_matrix(2)->dense(),
        model->series_specific_model(
            special_series)->state_transition_matrix(2)->dense()));

    EXPECT_TRUE(MatrixEquals(
        temp_model.state_variance_matrix(2)->dense(),
        model->series_specific_model(
            special_series)->state_variance_matrix(2)->dense()));

    EXPECT_TRUE(MatrixEquals(
        temp_model.state_error_expander(2)->dense(),
        model->series_specific_model(
            special_series)->state_error_expander(2)->dense()));

    EXPECT_TRUE(MatrixEquals(
        temp_model.state_error_variance(2)->dense(),
        model->series_specific_model(
            special_series)->state_error_variance(2)->dense()));

    EXPECT_TRUE(VectorEquals(
        temp_model.observation_matrix(2).dense(),
        model->series_specific_model(
            special_series)->observation_matrix(2).dense()));

    EXPECT_NEAR(square(residual_sd),
                model->series_specific_model(
                    special_series)->observation_variance(2),
                1e-8);

    EXPECT_TRUE(MatrixEquals(
        model->series_specific_model(special_series)->initial_state_variance(),
        SpdMatrix(6, 100.0)));
    
    //----------------------------------------------------------------------
    // Add Sampler for the model.
    //----------------------------------------------------------------------
    NEW(MultivariateStateSpaceRegressionPosteriorSampler, sampler)(model.get());
    model->set_method(sampler);

    //==========================================================================
    // Do the simulation
    //==========================================================================
    int burn = 20;
    for (int i = 0; i < burn; ++i) {
      model->sample_posterior();
    }
    int niter = 100;
    Matrix seasonal_effect_draws(niter, sample_size);
    Array coefficient_draws(std::vector<int>{niter, nseries, xdim});
    Matrix residual_sd_draws(niter, nseries);
    Matrix innovation_sd_draws(niter, nfactors);
    Vector seasonal_sd_draws(niter);
    
    for (int i = 0; i < niter; ++i) {
      //      model->sample_posterior();
      model->impute_state(GlobalRng::rng);
      seasonal_effect_draws.row(i) =
          model->series_specific_model(special_series)->state().row(0);
      seasonal_sd_draws[i] = seasonal_model->sigma();
      for (int j = 0; j < model->nseries(); ++j) {
        coefficient_draws.vector_slice(i, j, -1) =
            model->observation_model()->model(j)->Beta();
        residual_sd_draws(i, j) =
            model->observation_model()->model(j)->sigma();
      }
    }

    for (int i = 0; i < sample_size; ++i) {
      NEW(StateSpace::MultiplexedDoubleData, data_point)();
      data_point->add_data(new DoubleData(
          seasonal[i]
          + errors[i * nseries + special_series]));
      temp_model.add_data(data_point);
    }
    NEW(StateSpacePosteriorSampler, temp_model_sampler)(&temp_model);
    temp_model.observation_model()->set_sigsq(square(residual_sd));
    temp_model.set_method(temp_model_sampler);
    Matrix temp_model_state_draws(niter, sample_size);
    for (int i = 0; i < niter; ++i) {
      temp_model.sample_posterior();
      temp_model_state_draws.row(i) = temp_model.state_contribution(0);
    }
  }
  
}  // namespace
