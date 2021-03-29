export function resolve(proposal, proposerId, votes) {
    let memberVoteCount = 0;
    for (const vote of votes) {
        if (vote.vote) {
            memberVoteCount++;
        }
    }

    let activeMemberCount = 0;
    ccf.kv["public:ccf.gov.members.info"].forEach(v => {
        const info = ccf.bufToJsonCompatible(v);
        if (info.status === "Active") {
            activeMemberCount++;
        }
    });

    // A majority of members can accept a proposal.
    if (memberVoteCount > Math.floor(activeMemberCount / 2)) {
        return "Accepted";
    }

    return "Open";
}