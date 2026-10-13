import Foundation
import Combine
import UIKit
import HealthKit
import EventKit

/// Les données envoyées à l'écran
struct Stats {
    var batterie: Int?          // 0 à 100, nil si inconnue
    var enCharge = false
    var pas: Int?               // nil si indisponible
    var evenementHeure = ""     // ex. "Auj. 14:30", "Demain 09:00", "En cours"
    var evenementTitre = "Chargement…"
}

/// Récupère la batterie, les pas du jour (HealthKit) et le prochain événement (EventKit)
@MainActor
final class StatsCollector: ObservableObject {

    @Published var stats = Stats()

    private let sante = HKHealthStore()
    private let calendrier = EKEventStore()
    private var autorisationsDemandees = false

    init() {
        UIDevice.current.isBatteryMonitoringEnabled = true
    }

    func actualiser() async {
        await demanderAutorisations()

        var s = Stats()

        // Batterie
        let niveau = UIDevice.current.batteryLevel
        s.batterie = niveau < 0 ? nil : Int((niveau * 100).rounded())
        let etat = UIDevice.current.batteryState
        s.enCharge = (etat == .charging || etat == .full)

        // Pas
        s.pas = await lirePasDuJour()

        // Calendrier
        (s.evenementHeure, s.evenementTitre) = prochainEvenement()

        stats = s
    }

    // MARK: - Autorisations

    private func demanderAutorisations() async {
        guard !autorisationsDemandees else { return }
        autorisationsDemandees = true

        if HKHealthStore.isHealthDataAvailable() {
            try? await sante.requestAuthorization(toShare: [], read: [HKQuantityType(.stepCount)])
        }
        _ = try? await calendrier.requestFullAccessToEvents()
    }

    // MARK: - HealthKit

    private func lirePasDuJour() async -> Int? {
        guard HKHealthStore.isHealthDataAvailable() else { return nil }

        let type = HKQuantityType(.stepCount)
        let debut = Calendar.current.startOfDay(for: .now)
        let predicat = HKQuery.predicateForSamples(withStart: debut, end: .now)
        let requete = HKStatisticsQueryDescriptor(
            predicate: .quantitySample(type: type, predicate: predicat),
            options: .cumulativeSum
        )

        do {
            let resultat = try await requete.result(for: sante)
            let total = resultat?.sumQuantity()?.doubleValue(for: .count()) ?? 0
            return Int(total)
        } catch {
            return nil
        }
    }

    // MARK: - EventKit

    private func prochainEvenement() -> (String, String) {
        guard EKEventStore.authorizationStatus(for: .event) == .fullAccess else {
            return ("", "Calendrier refusé")
        }

        let maintenant = Date()
        let dans7Jours = Calendar.current.date(byAdding: .day, value: 7, to: maintenant)!
        let predicat = calendrier.predicateForEvents(withStart: maintenant, end: dans7Jours, calendars: nil)

        let evenements = calendrier.events(matching: predicat)
            .filter { !$0.isAllDay }
            .sorted { $0.startDate < $1.startDate }

        guard let e = evenements.first else { return ("", "Aucun événement") }

        let titre = (e.title?.isEmpty == false) ? e.title! : "Sans titre"
        if e.startDate <= maintenant { return ("En cours", titre) }

        let format = DateFormatter()
        format.locale = Locale(identifier: "fr_FR")
        let cal = Calendar.current

        if cal.isDateInToday(e.startDate) {
            format.dateFormat = "HH:mm"
            return ("Auj. " + format.string(from: e.startDate), titre)
        }
        if cal.isDateInTomorrow(e.startDate) {
            format.dateFormat = "HH:mm"
            return ("Demain " + format.string(from: e.startDate), titre)
        }
        format.dateFormat = "EEE d 'à' HH:mm"
        return (format.string(from: e.startDate), titre)
    }
}
