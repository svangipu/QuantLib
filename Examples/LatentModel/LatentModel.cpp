/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2014 Jose Aparicio

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/quantlib.hpp>

#include <boost/timer.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>

#ifdef BOOST_MSVC
#  include <ql/auto_link.hpp>
#  define BOOST_LIB_NAME boost_thread
#  include <boost/config/auto_link.hpp>
#  define BOOST_LIB_NAME boost_system
#  include <boost/config/auto_link.hpp>
#  undef BOOST_LIB_NAME
#endif

#include <iostream>
#include <iomanip>

using namespace std;
using namespace QuantLib;

#if defined(QL_ENABLE_SESSIONS)
namespace QuantLib {

    Integer sessionId() { return 0; }

}
#endif


namespace {
    void nodeletion(DefaultLossModel*) {}
}

/* This sample code shows basic usage of a Latent variable model.
   The data and correlation problem presented is the same as in:
     'Modelling Dependent Defaults: Asset Correlations Are Not Enough!'
     Frey R., A. J. McNeil and M. A. Nyfeler RiskLab publications March 2001
*/
int main(int, char* []) {

    try {

        boost::timer timer;
        std::cout << std::endl;

        Calendar calendar = TARGET();
        Date todaysDate(19, March, 2014);
        // must be a business day
        todaysDate = calendar.adjust(todaysDate);

        Settings::instance().evaluationDate() = todaysDate;


        /* --------------------------------------------------------------
                        SET UP BASKET PORTFOLIO
        -------------------------------------------------------------- */
        // build curves and issuers into a basket of three names
        std::vector<Real> hazardRates(3, -std::log(1.-0.01));
        std::vector<std::string> names;
        for(Size i=0; i<hazardRates.size(); i++)
            names.push_back(std::string("Acme") + 
                boost::lexical_cast<std::string>(i));
        std::vector<Handle<DefaultProbabilityTermStructure> > defTS;
        for(Size i=0; i<hazardRates.size(); i++)
            defTS.push_back(Handle<DefaultProbabilityTermStructure>(
                boost::make_shared<FlatHazardRate>(0, TARGET(), hazardRates[i], 
                    Actual365Fixed())));
        std::vector<Issuer> issuers;
        for(Size i=0; i<hazardRates.size(); i++) {
            std::vector<QuantLib::Issuer::key_curve_pair> curves(1, 
                std::make_pair(NorthAmericaCorpDefaultKey(
                    EURCurrency(), QuantLib::SeniorSec,
                    Period(), 1. // amount threshold
                    ), defTS[i]));
            issuers.push_back(Issuer(curves));
        }

        boost::shared_ptr<Pool> thePool = boost::make_shared<Pool>();
        for(Size i=0; i<hazardRates.size(); i++)
            thePool->add(names[i], issuers[i], NorthAmericaCorpDefaultKey(
                    EURCurrency(), QuantLib::SeniorSec, Period(), 1.));

        std::vector<DefaultProbKey> defaultKeys(hazardRates.size(), 
            NorthAmericaCorpDefaultKey(EURCurrency(), SeniorSec, Period(), 1.));
        // Recoveries are irrelevant in this example but must be given as the 
        //   lib stands.
        std::vector<boost::shared_ptr<RecoveryRateModel> > rrModels(
            hazardRates.size(), boost::make_shared<ConstantRecoveryModel>(
            ConstantRecoveryModel(0.5, SeniorSec)));
        boost::shared_ptr<Basket> theBskt = boost::make_shared<Basket>(
            todaysDate, names, std::vector<Real>(hazardRates.size(), 100.), 
            thePool);
        boost::shared_ptr<Basket> theBskt2 = boost::make_shared<Basket>(
            todaysDate, names, std::vector<Real>(hazardRates.size(), 100.), 
            thePool, 0., .75);

        /* --------------------------------------------------------------
                        SET UP JOINT DEFAULT EVENT LATENT MODELS
        -------------------------------------------------------------- */
        // Latent model factors, corresponds to the first entry in Table1 of the
        //   publication mentioned. It is a single factor model
        std::vector<std::vector<Real> > fctrsWeights(hazardRates.size(), 
            std::vector<Real>(1, std::sqrt(0.1)));
        // Gaussian integrable joint default model:
        GaussianDefProbLM lmG(/*theBskt, */fctrsWeights, 
            LatentModelIntegrationType::GaussianQuadrature,
			GaussianCopulaPolicy::initTraits() // otherwise gcc screams
			);
 /// TTHROUGH THE BASKET NOW , has to set the model              lmG.resetBasket(theBskt);
        // Gaussian random joint default model:
        Size numSimulations = 1000;
        Size numCoresUsed = 4;
        // Sobol, many cores
        RandomDefaultLM<GaussianCopulaPolicy> rdlmG(
            /*theBskt,*/ lmG, std::vector<Real>(), numSimulations, 1.e-6, 2863311530);
        // Monothread only
        /*
        RandomDefaultLM<GaussianCopulaPolicy, 
            RandomSequenceGenerator<MersenneTwisterUniformRng> > rdlmG(
            theBskt, lmT, std::vector<Real>(), numSimulations, 1.e-6, 2863311530);
        */
        // Define StudentT copula
        // this is as far as we can be from the Gaussian
        std::vector<Integer> ordersT(2, 5);
        TCopulaPolicy::initTraits iniT;
        iniT.tOrders = ordersT;
        // StudentT integrable joint default model:
        TDefProbLM lmT(/*theBskt, */fctrsWeights, 
            LatentModelIntegrationType::GaussianQuadrature, iniT);
 /// TTHROUGH THE BASKET NOW , has to set the model      lmT.resetBasket(theBskt);
        // StudentT random joint default model:
        //  --Not showing the correct default correlation!!-------------------
        // Sobol, many cores
        boost::shared_ptr<DefaultLossModel> rdlmT(boost::make_shared<RandomDefaultLM<TCopulaPolicy> >(lmT, std::vector<Real>(), numSimulations, 1.e-6, 2863311530));
        /*
        // Monothread only, direct/copula inversion
        RandomDefaultLM<TCopulaPolicy, RandomSequenceGenerator<MersenneTwisterUniformRng> > rdlmT(
            theBskt, lmT, std::vector<Real>(), numSimulations, 1.e-6, 2863311530);
*/
        /* Goes through two partial spezs, number look worst.
        RandomDefaultLM<TCopulaPolicy, RandomSequenceGenerator<PolarStudentTRng<MersenneTwisterUniformRng> > > rdlmT(
            theBskt, lmT, std::vector<Real>(), numSimulations, 1.e-6, 2863311530);
        */

        /* --------------------------------------------------------------
                        DUMP SOME RESULTS
        -------------------------------------------------------------- */
        /* Correlations in a T copula should be below those of the gaussian
        for the same factors.
        The calculations on the MC show dispersion on both copulas (thats
        ok) and too large values with very large dispersions on the T case.
        Computations are ok, within the dispersion, for the gaussian; compare
        with the direct integration in both cases.
        However the T does converge to the gaussian value for large value of
        the parameters.
        */
        std::cout << 
            "T versus Gaussian prob of extreme event (random and integrable)-" 
            << std::endl;
        Date calcDate(TARGET().advance(Settings::instance().evaluationDate(), 
            Period(120, Months)));
      //  theBskt->setLossModel(boost::shared_ptr<DefaultLossModel>(rdlmT));
        theBskt->setLossModel(rdlmT);
       for(Size numEvts=0; numEvts <=3; numEvts++) {
            std::cout << "-Prob of " << 3 << " events... " <<
            theBskt->probAtLeastNEvents(numEvts, calcDate)
            << std::endl;
        }

Real etl_ = theBskt->probAtLeastNEvents(2, calcDate);

        theBskt2->setLossModel(rdlmT);
       //now it should reset the arg basket prior in the loss model
        Probability tititi = theBskt2->probAtLeastNEvents(1, calcDate);
        // bskt1 calc=false now?







 ////       theBskt->setLossModel(boost::shared_ptr<DefaultLossModel>(&rdlmG, nodeletion));

etl_ = theBskt->probAtLeastNEvents(2, calcDate);

        for(Size numEvts=0; numEvts <=3; numEvts++) {
            std::cout << "-Prob of " << 3 << " events... " <<
            theBskt->probAtLeastNEvents(numEvts, calcDate)
            << std::endl;
        }
        lmT.resetBasket(theBskt);
        lmG.resetBasket(theBskt);
        for(Size numEvts=0; numEvts <=3; numEvts++) {
            std::cout << "-Prob of " << 3 << " events... " <<
          //  rdlmT.probAtLeastNEvents(numEvts, calcDate)
          //  << " ... " <<
            //rdlmG.probAtLeastNEvents(numEvts, calcDate) 
            //<< " ... " <<
            lmT.probAtLeastNEvents(numEvts, calcDate)
            << " ... " <<
            lmG.probAtLeastNEvents(numEvts, calcDate) 
            << std::endl;
        }

        Date correlDate = TARGET().advance(
            Settings::instance().evaluationDate(), Period(12, Months));
        std::vector<std::vector<Real> > correlsG, correlsT;
        for(Size iName1=0; iName1 <3; iName1++) {
            std::vector<Real> tmpG, tmpT;
            for(Size iName2=0; iName2 <3; iName2++) {
                tmpG.push_back(lmG.defaultCorrelation(correlDate, 
                    iName1, iName2));
                tmpT.push_back(lmT.defaultCorrelation(correlDate, 
                    iName1, iName2));
            }
            correlsG.push_back(tmpG);
            correlsT.push_back(tmpT);
        }
        cout << endl;
        cout << "------Gaussian default correlations---" << endl;
        cout << "--------------------------------------" << endl;
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << correlsG[iName1][iName2] << " * ";
            cout << endl;
        }
        cout << endl;
        cout << "------StudeT default correlations---" << endl;
        cout << "------------------------------------" << endl;
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << correlsT[iName1][iName2] << " * ";
            cout << endl;
        }
        cout << endl;
        cout << "------Gaussian asset correlations-----" << endl;
        cout << "--------------------------------------" << endl;
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << lmG.latentVariableCorrel(iName1, iName2) << " * ";
            cout << endl;
        }
        cout << endl;
        cout << "------StudeT asset correlations-----" << endl;
        cout << "------------------------------------" << endl;
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << lmT.latentVariableCorrel(iName1, iName2) << " * ";
            cout << endl;
        }
        cout << endl;
        cout << "----Gaussian rand def correlations----" << endl;
        cout << "--------------------------------------" << endl;
        // too bad, runs the MC again
  ///////////////      theBskt->setLossModel(boost::shared_ptr<DefaultLossModel>(&rdlmG));
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << theBskt->defaultCorrelation(correlDate, iName1, iName2) 
                    << " * ";
            cout << endl;
        }
        cout << endl;
        cout << "---- StudenT rand def correlations----" << endl;
        cout << "--------------------------------------" << endl;
        // too bad, runs the MC again
        theBskt->setLossModel(boost::shared_ptr<DefaultLossModel>(rdlmT));
        for(Size iName1=0; iName1 <3; iName1++) {
            for(Size iName2=0; iName2 <3; iName2++)
                cout << theBskt->defaultCorrelation(correlDate, iName1, iName2) 
                    << " * ";
            cout << endl;
        }


        Real seconds  = timer.elapsed();
        Integer hours = Integer(seconds/3600);
        seconds -= hours * 3600;
        Integer minutes = Integer(seconds/60);
        seconds -= minutes * 60;
        cout << "Run completed in ";
        if (hours > 0)
            cout << hours << " h ";
        if (hours > 0 || minutes > 0)
            cout << minutes << " m ";
        cout << fixed << setprecision(0)
             << seconds << " s" << endl;

        return 0;
    } catch (exception& e) {
        cerr << e.what() << endl;
        return 1;
    } catch (...) {
        cerr << "unknown error" << endl;
        return 1;
    }
}

