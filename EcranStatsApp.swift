import SwiftUI

@main
struct EcranStatsApp: App {
    @StateObject private var ble = BLEManager()
    @StateObject private var collecteur = StatsCollector()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
                .environmentObject(collecteur)
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var collecteur: StatsCollector

    var body: some View {
        NavigationStack {
            List {
                Section("Arduino") {
                    HStack(spacing: 10) {
                        Circle()
                            .fill(ble.pret ? .green : .orange)
                            .frame(width: 10, height: 10)
                        Text(ble.etat)
                    }
                }

                Section("Affiché sur l'écran") {
                    LabeledContent("Batterie", value: texteBatterie)
                    LabeledContent("Pas aujourd'hui", value: collecteur.stats.pas.map { "\($0)" } ?? "Indisponible")
                    LabeledContent(
                        collecteur.stats.evenementHeure.isEmpty ? "Événement" : collecteur.stats.evenementHeure,
                        value: collecteur.stats.evenementTitre
                    )
                }

                Section {
                    Button("Envoyer maintenant") {
                        Task { await actualiserEtEnvoyer() }
                    }
                    .disabled(!ble.pret)
                } footer: {
                    Text("Les données sont envoyées toutes les minutes tant que l'app est ouverte.")
                }
            }
            .navigationTitle("Écran de stats")
        }
        // Envoi automatique toutes les 60 secondes
        .task {
            while !Task.isCancelled {
                await actualiserEtEnvoyer()
                try? await Task.sleep(for: .seconds(60))
            }
        }
        // Envoi immédiat dès que l'Arduino est connecté
        .onChange(of: ble.pret) { _, pret in
            if pret { Task { await actualiserEtEnvoyer() } }
        }
    }

    private var texteBatterie: String {
        guard let b = collecteur.stats.batterie else { return "Inconnue" }
        return "\(b) %" + (collecteur.stats.enCharge ? " (en charge)" : "")
    }

    @MainActor
    private func actualiserEtEnvoyer() async {
        await collecteur.actualiser()
        ble.envoyer(collecteur.stats)
    }
}
