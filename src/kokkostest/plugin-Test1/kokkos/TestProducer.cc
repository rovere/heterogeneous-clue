#include <iostream>
#include <thread>

#include "DataFormats/FEDRawDataCollection.h"
#include "Framework/EDProducer.h"
#include "Framework/Event.h"
#include "Framework/EventSetup.h"
#include "Framework/PluginFactory.h"

#include "KokkosCore/kokkosConfig.h"
#include "KokkosCore/Product.h"
#include "KokkosCore/ScopedContext.h"

#include "kokkosAlgo1.h"

namespace KOKKOS_NAMESPACE {
  class TestProducer : public edm::EDProducer {
  public:
    explicit TestProducer(edm::ProductRegistry& reg);

  private:
    void produce(edm::Event& event, edm::EventSetup const& eventSetup) override;

    edm::EDGetTokenT<FEDRawDataCollection> rawGetToken_;
    edm::EDPutTokenT<cms::kokkos::Product<Kokkos::View<const float*, KokkosExecSpace>>> putToken_;
  };

  TestProducer::TestProducer(edm::ProductRegistry& reg)
      : rawGetToken_(reg.consumes<FEDRawDataCollection>()),
        putToken_(reg.produces<cms::kokkos::Product<Kokkos::View<const float*, KokkosExecSpace>>>()) {}

  void TestProducer::produce(edm::Event& event, edm::EventSetup const& eventSetup) {
    auto const value = event.get(rawGetToken_).FEDData(1200).size();
    std::cout << "TestProducer  Event " << event.eventID() << " stream " << event.streamID() << " ES int "
              << eventSetup.get<int>() << " FED 1200 size " << value << std::endl;

    cms::kokkos::ScopedContextProduce<KokkosExecSpace> ctx;
    ctx.emplace(event, putToken_, kokkosAlgo1(ctx.execSpace()));
  }
}  // namespace KOKKOS_NAMESPACE

DEFINE_FWK_KOKKOS_MODULE(TestProducer);
